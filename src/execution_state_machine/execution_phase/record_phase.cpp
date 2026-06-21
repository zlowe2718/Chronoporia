#include "record_phase.h"
#include "debugger_transition.h"
#include "error_transition.h"
#include "nt_wrappers.h"
#include "reconstruction_transition.h"
#include "record_transition.h"
#include "thread_utils.h"
#include "time_restore_transition.h"
#include "trampoline_manager.h"
#include "breakpoint_manager.h"
#include "thread_manager.h"
#include "globals.h"
#include "event_log.h"
#include "module_manager.h"
#include "module_utils.h"
#include "snapshot_log.h"
#include <chrono>
#include <minwinbase.h>


namespace chronoporia {

    void RecordingPhase::Enter() {
        child_entry_address = GetChildEntryAddress();

        if (process_suspended) {
            ResumeProcess();
        }
    }

    Transitions RecordingPhase::Run() {
        ResumeProcess();
        return DebugLoop();
    }

    void RecordingPhase::Exit() {}

    Transitions RecordingPhase::DebugLoop() {
        DEBUG_EVENT de;
        DWORD last_error;
        bool debug_event_success;

        long long wait_timeout = globals::snapshot_interval * 1000; //microseconds
        auto execution_resume_time = std::chrono::system_clock::now();

        while (globals::running) {
            debug_event_success = WaitForDebugEvent(&de, wait_timeout / 1000); //convert back to milliseconds
            auto execution_stop_time = std::chrono::system_clock::now();

            if (debug_event_success) {
                long long microseconds_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(execution_stop_time - execution_resume_time).count(); 
                wait_timeout -= microseconds_elapsed;

                if (wait_timeout < 0) wait_timeout = 0;

                DWORD continue_status = DBG_CONTINUE;

                switch (de.dwDebugEventCode) {

                    case EXCEPTION_DEBUG_EVENT:
                        continue_status = HandleDebugException(&de);
                        break;
                    case LOAD_DLL_DEBUG_EVENT: {
                        LOAD_DLL_DEBUG_INFO debug_info = de.u.LoadDll;
                        std::wstring image_name = GetDllNameFromLp(debug_info.lpImageName);
                        CloseHandle(debug_info.hFile);
                        TrackDLL(reinterpret_cast<HMODULE>(debug_info.lpBaseOfDll), image_name, globals::global_sequence, globals::run_id, globals::run_sequence);
                        break;
                    }
                    case UNLOAD_DLL_DEBUG_EVENT: {
                        LOAD_DLL_DEBUG_INFO debug_info = de.u.LoadDll;
                        UntrackDLL(reinterpret_cast<HMODULE>(debug_info.lpBaseOfDll), globals::global_sequence, globals::run_id, globals::run_sequence);
                        break;
                    }
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

                // TODO: may need to later account for extra events coming in after process was suspended
                // timeout occurred, take a snapshot.  Threads are NOT suspended on a timeout
                if (last_error == ERROR_SEM_TIMEOUT) {
                    SuspendProcess();
                    // TODO: Fix this later with a better way to transition back to the debugger 
                    return TransitionToTimeRestore {true, 0, 0, TransitionsBox{Transitions{TransitionToReconstruction {true}}}};
                } else {
                    printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                    globals::running = false;
                    return TransitionToError {false, last_error};
                }
                execution_resume_time = std::chrono::system_clock::now();
            }
        }

        return TransitionToError {false, 0};
    }

    DWORD RecordingPhase::HandleDebugException(const DEBUG_EVENT* debug_event) {
        const EXCEPTION_RECORD* er = &debug_event->u.Exception.ExceptionRecord;

        switch (er->ExceptionCode) {
            case STATUS_BREAKPOINT: {
                return HandleBreakpoint(debug_event);
            }

            // case STATUS_GUARD_PAGE_VIOLATION: {
            //     // TODO: Do I need to worry about stack overflows messing with this?
            //     ULONG_PTR fault_address = er->ExceptionInformation[1];
            //     ULONG_PTR fault_page_start_addr = fault_address & ~0xFFFULL; //reset the page nibble

            //     if (!WasPageArmed(reinterpret_cast<void *>(fault_page_start_addr))) {
            //         return DBG_EXCEPTION_NOT_HANDLED;
            //     }

            //     TrackDirtyPage(reinterpret_cast<void *>(fault_page_start_addr));
            //     return DBG_CONTINUE;
            // }

            // We haven't handled this case yet log and print what happened
            // default: {
            //     CONTEXT ctx = GetThreadContextFromId(debug_event->dwThreadId);
            //     MEMORY_BASIC_INFORMATION m_addr;
            //     MEMORY_BASIC_INFORMATION m_rip;
            //     MEMORY_BASIC_INFORMATION m_rsp;
            //     VirtualQueryEx(globals::process_handle, (void*)er->ExceptionAddress, &m_addr, sizeof(m_addr));
            //     VirtualQueryEx(globals::process_handle, (void*)ctx.Rip, &m_rip, sizeof(m_rip));
            //     VirtualQueryEx(globals::process_handle, (void*)ctx.Rsp, &m_rsp, sizeof(m_rsp));
            //     printf("(Thread: %ld) Unhandled Debug Exception: \n"
            //            "    Exception Code: %ld\n"
            //            "    Exception Address: %p\n"
            //            "    Thread Rip: %p\n"
            //            "    Thread Rsp: %p\n"
            //         , debug_event->dwThreadId
            //         , er->ExceptionCode
            //         , ctx.Rip
            //         , ctx.Rsp
            //     );
            //     return DBG_EXCEPTION_NOT_HANDLED;
            // }
        }

        return DBG_EXCEPTION_NOT_HANDLED;
    }

    DWORD RecordingPhase::HandleBreakpoint(const DEBUG_EVENT *debug_event) {
        DWORD thread_id = debug_event->dwThreadId;
        // Need to roll back one instruction as the rip stores the pointer to the next instruction
        uintptr_t rip_address = GetRipAddress(thread_id) - 1;
        auto bp_type = GetBreakpointType(rip_address, thread_id);

        CONTEXT ctx = RollBackInstructionPointRegister(thread_id);
        if (rip_address == child_entry_address) {
            RemoveBreakpoint(child_entry_address, globals::main_thread_id);
            TrackAllProgramThreads(globals::global_sequence, globals::run_id, globals::run_sequence);
            
            // I'm running this before the process snapshot so the memory snapshots are consistent and because its
            // useful to have the permanent breakpoint be there permanently for now.
            LoadTrampoline();
            StartSnapshotHistory();
            SnapshotProcess(SnapshotType::CoarseSnapshot);
        } else {
            switch (bp_type) {
                case BreakpointType::Return: {
                    OnBreakpointReturn(rip_address, thread_id, ctx);
                    break;
                }
                case BreakpointType::Permanent: {
                    OnBreakpointEnter(rip_address, thread_id);
                    break;
                }
                case BreakpointType::SpinLock: {
                    break;
                }
            }
        }
        return DBG_CONTINUE;
    }
}