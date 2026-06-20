#include "reconstruction_phase.h"
#include "error_transition.h"
#include "globals.h"
#include "debugger_transition.h"
#include "snapshot_log.h"
#include "thread_utils.h"
#include "trampoline.h"
#include "breakpoint_manager.h"
#include "nt_wrappers.h"
#include "transition.h"
#include "event_log.h"
#include <chrono>

/** TODO: Goals to accomplish here 
* Have plugin load breakpoints at all lines of the program 
* Run plugin initialization function
* Place breakpoint at record code stop and run until code end breakpoint is hit
* Track breakpoints at code addresses as UserBreakpoints
* On UserBreakpoint hit, run the plugin ReachedBreakpoint call to determine if valid
* If breakpoint valid then create a line snapshot event
* Line snapshot will snapshot all thread contexts and only dirty memory
* If we hit a non-det event then either inject values to replay or return the saved value as needed
* Make a per event thread log
* Compare events that a thread hits to per-thread event log to determine value inject or return replay (or both if side effects happen behind the scenes)
* Recreate event_log events with the new Line Snapshot events -> Start from coarse snapshot event and as new events come in compare to old event log and add them in the new order
* On code end breakpoint hit transition to debugger phase. Debugger phase will allow the user to jump between lines and inspect memory like a normal debugger.
* When jumping between lines may want to manually call prints that would normally happen
* Maybe a separate mode where user selects if they want to jump forward in memory or have the program execute forward in memory.  Going backwards still needs to jump back in memory
*
* Because there could be millions to billions of c++ lines run in a second on modern processors, lets change up the recording behavior.
* Instead I'll take memory snapshots every second (user specifiable) until the end reached.  then in playback the user can jump between 
* Snapshots and when stepping forward we take a line snapshot and cache the line so the user can jump back?  I would then need to invalidate any future cached lines if I step backwards
* I.e I start at miniSnap 5 then break at line 10, resuming forward execution to break at line 11 and 12 means I can safely jump back to those cached lines.  However if I jump back to 10 then want
* to step back to 9 I need to start from miniSnap5 and replay to 9.  I need to invalidate any cached lines in front of that because we could have a situation when thread 2 reached line 123 in another file
* when thread 1 reached 10, but upon restarting from miniSnap5 maybe thread 2 reached line 125 when thread 1 reached line 9.  This means that another thread is ahead of a previous cache and so stepping forward
* would give an odd result making it look like another thread went backwards.
*
* This method of caching and replaying should suffice for most user needs since people will probably be digging around in the same chunk of code most of the time.  We could even have the user pre-specify where to start
* caching lines and how many lines or specific lines to cache.  Maybe also have a setting to save old cached branches (cached lines like 10, 12, 13, 15, but restarted code execution from 9 so 10 and beyond now live on a
* branch that the user can choose to prune (delete) or persist. 
*
* TODO: For multiple threads, deconflict non-det events when they could be called out of order in the future.  I.e. run 1 -> thread 1 calls WaitForSingleObject then thread 2 calls WaitForSingleObject, but on run 2
* thread 2 calls WaitForSingleObject first.  Probably need to match function args and thread context?
*/

namespace chronoporia {

    void ReconstructionPhase::Enter() {
        globals::run_id += 1;
    }


    Transition ReconstructionPhase::Run() {
        DEBUG_EVENT de;
        DWORD last_error;
        bool debug_event_success;

        long long wait_timeout = 1000; // milliseconds
        long long wait_timeout_us = wait_timeout * 1000; // microseconds
        auto execution_resume_time = std::chrono::system_clock::now();
        uint32_t snapshots_remaining = globals::snapshot_interval / wait_timeout;

        while (globals::running) {
            debug_event_success = WaitForDebugEvent(&de, wait_timeout_us / 1000); //convert back to milliseconds
            auto execution_stop_time = std::chrono::system_clock::now();

            if (debug_event_success) {
                long long microseconds_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(execution_stop_time - execution_resume_time).count(); 
                wait_timeout_us -= microseconds_elapsed;

                if (wait_timeout_us < 0) wait_timeout_us = 0;

                DWORD continue_status = DBG_CONTINUE;

                switch (de.dwDebugEventCode) {

                    case EXCEPTION_DEBUG_EVENT:
                        continue_status = HandleDebugException(&de);
                        break;
                    case EXIT_PROCESS_DEBUG_EVENT:
                        globals::running = false;
                        continue_status = DBG_EXCEPTION_NOT_HANDLED;
                        printf("Process exiting with error code %lx\n", de.u.ExitProcess.dwExitCode);
                        break;
                }

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );
                execution_resume_time = std::chrono::system_clock::now();
            } else {
                last_error = GetLastError();

                if (last_error == ERROR_SEM_TIMEOUT) {
                    SuspendProcess();
                    SnapshotProcess(SnapshotType::MicroSnapshot);

                    snapshots_remaining -= 1;
                    // We've taken all the micro snapshots needed.  Now we can act like a normal debugger
                    if (snapshots_remaining == 0) {
                        return TransitionToDebugger {};
                    }

                    ResumeProcess();
                } else {
                    printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                    globals::running = false;
                    return TransitionToError {false, last_error};
                }
                execution_resume_time = std::chrono::system_clock::now();
            }
        }

        return TransitionToError {0};
    }

    void ReconstructionPhase::Exit() {}

    DWORD ReconstructionPhase::HandleDebugException(const DEBUG_EVENT* debug_event) {
        const EXCEPTION_RECORD* er = &debug_event->u.Exception.ExceptionRecord;

        switch (er->ExceptionCode) {
            case STATUS_BREAKPOINT: {
                return HandleBreakpoint(debug_event);
            }
        }

        return DBG_EXCEPTION_NOT_HANDLED;
    }

    DWORD ReconstructionPhase::HandleBreakpoint(const DEBUG_EVENT *debug_event) {
        DWORD thread_id = debug_event->dwThreadId;
        // Need to roll back one instruction as the rip stores the pointer to the next instruction
        uintptr_t rip_address = GetRipAddress(thread_id) - 1;
        auto bp_type = GetBreakpointType(rip_address, thread_id);

        switch (bp_type) {
            case BreakpointType::Permanent: {
                ReplayEvent(rip_address, thread_id);
                break;
            }
            case BreakpointType::Return: {
                ReplayEventEnd(rip_address, thread_id);
                break;
            }
            case BreakpointType::SpinLock: {
                break;
            }
        }
        return DBG_CONTINUE;
    }


}