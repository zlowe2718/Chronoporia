#include "time_restore_phase.h"
#include "error_transition.h"
#include "reconstruction_transition.h"
#include "shellcode.h"
#include "breakpoint_manager.h"
#include "module_manager.h"
#include "thread_manager.h"
#include "memory_manager.h"
#include "globals.h"
#include "thread_utils.h"
#include "nt_wrappers.h"
#include "trampoline.h"
#include "transition.h"
#include <errhandlingapi.h>
#include <future>
#include <minwinbase.h>
#include <winbase.h>
#include <chrono>
#include <winnt.h>

using namespace std::chrono_literals;

// We need to unload/laod dlls and destroy/create threads in a separate thread.  The reason for this
// is because we're subscribed to debug events that windows will send us debug events as they occur.
// If we create a thread in the target process in the debugger main thread then the windows kernel
// recognizes that the target process has a debugger attached to it and gives the debugger the 
// opportunity to handle it first before the call resolves.  Since our debugger main thread is waiting  
// for the create thread call to resolve however we can't handle the debug events.
// 
// I'm using futures with a wait time of 0 to essentially spin lock the debugger process so we know exactly
// when the thread is finished without worrying about timeouts


// TODO: Make time restoration a util instead of a phase?  I need to know which phase to move to next
namespace chronoporia {
    // TODO: reenable shell code execution.  Order should be dll -> threads -> memory
    // During the dll and thread fixing we'll need to be running WaitForDebugEvent so the process
    // Can proceed with unloading/loading dlls and destroying/creating threads
    void TimeRestorePhase::Enter() {
        auto dlls_to_unload = RestoreDLLsAtSequence(globals::run_id, globals::run_sequence, target_run_id_, target_run_sequence_);

        code_size_ = AddDllUnload(dlls_to_unload);
        code_address_ = WriteShellCodeBufferToProcess();

        TrackShellCodeBreakpoint(code_address_ + code_size_ - 1);

        // Thread hijacking to run shell code
        HANDLE hThread = GetThreadHandle(globals::main_thread_id);
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(hThread, &ctx);

        ctx.Rip = code_address_;
        // Align RSP to 16 bytes — the sub rsp, 0x28 in the
        // shellcode will maintain alignment from here
        ctx.Rsp = ctx.Rsp & ~0xFULL;
        SetThreadContext(hThread, &ctx);

    }

    Transitions TimeRestorePhase::Run() {
        if (!RunDllRestore()) {
            assert(false);
            return TransitionToError {false, GetLastError()};
        }
        if (!RunThreadRestore()) {
            assert(false);
            return TransitionToError {false, GetLastError()};
        }
        return std::move(*next_transition_.value);
    }

    // TODO: When loading dlls (calling RestoreDLLsAtSequence) I'll need to eventually need to move that under the async call like threads and probably combine these functions
    bool TimeRestorePhase::RunDllRestore() {
        DEBUG_EVENT de;
        DWORD last_error;
        bool debug_event_success;

        ResumeThreadId(globals::main_thread_id);
        while (true) {
            // Do I need to resume the process here?
            debug_event_success = WaitForDebugEvent(&de, INFINITE);

            if (debug_event_success) {
                DWORD continue_status = DBG_CONTINUE;

                switch (de.dwDebugEventCode) {
                    case LOAD_DLL_DEBUG_EVENT:
                        printf("dll loaded\n");
                        break;
                    case UNLOAD_DLL_DEBUG_EVENT:
                        printf("dll unloaded\n");
                        break;
                    case EXCEPTION_DEBUG_EVENT:
                        const EXCEPTION_RECORD& er = de.u.Exception.ExceptionRecord;

                        // Since permanent breakpoints are permanent check what type of breakpoint it is and handle accordingly
                        if (er.ExceptionCode == STATUS_BREAKPOINT) {

                            DWORD thread_id = de.dwThreadId;
                            uintptr_t rip_address = GetRipAddress(thread_id) - 1;
                            auto bp_type = GetBreakpointType(rip_address, thread_id);

                            switch (bp_type) {
                                // Permanent breakpoints just need to run the trampoline and continue
                                case BreakpointType::Permanent: {
                                    RedirectToTrampoline(rip_address, thread_id);
                                    break;
                                }
                                case BreakpointType::ShellCode: {
                                    bool correct_shell_addr = code_address_ + code_size_ - 1 == rip_address;

                                    if (!correct_shell_addr) return false;

                                    SuspendThreadId(globals::main_thread_id);
                                    ContinueDebugEvent(
                                        de.dwProcessId,
                                        de.dwThreadId,
                                        continue_status
                                    );

                                    return true;
                                }
                                default: {
                                    return false;
                                }
                            }
                        }
                        break; 
                }

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );

            } else {
                last_error = GetLastError();

                printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                globals::running = false;
                return false;
            }                
        }    
    }

    bool TimeRestorePhase::RunThreadRestore() {
        DEBUG_EVENT de;
        DWORD last_error;
        bool debug_event_success;

        unload_dlls_and_threads_ = std::async(std::launch::async, [this]() {
            RestoreThreadsAtSequence(globals::run_id, globals::run_sequence, target_run_id_, target_run_sequence_);
        });
        std::future_status status = unload_dlls_and_threads_.wait_for(0ms);
        while (status != std::future_status::ready) {
            debug_event_success = WaitForDebugEvent(&de, 0);

            if (debug_event_success) {
                DWORD continue_status = DBG_CONTINUE;

                // TODO: check if STATUS_BREAKPOINT and if so check what type of breakpoint it is.  If permanent breakpoint then run RedirectToTrampoline
                // shouldn't need to handle anything else here since this is just to unblock the debugger main thread
                switch (de.dwDebugEventCode) {
                    case CREATE_THREAD_DEBUG_EVENT:
                        printf("Thread created\n");
                        break;
                    case EXIT_THREAD_DEBUG_EVENT:
                        printf("Thread exiting\n");
                        break;
                    case EXCEPTION_DEBUG_EVENT:
                        const EXCEPTION_RECORD& er = de.u.Exception.ExceptionRecord;

                        // Since permanent breakpoints are permanent check what type of breakpoint it is and handle accordingly
                        if (er.ExceptionCode == STATUS_BREAKPOINT) {

                            DWORD thread_id = de.dwThreadId;
                            uintptr_t rip_address = GetRipAddress(thread_id) - 1;
                            auto bp_type = GetBreakpointType(rip_address, thread_id);

                            switch (bp_type) {
                                // Permanent breakpoints just need to run the trampoline and continue
                                case BreakpointType::Permanent: {
                                    RedirectToTrampoline(rip_address, thread_id);
                                    break;
                                }
                                // This shouldn't happen, something went wrong 
                                default: {
                                    return false;
                                }
                            }
                        }
                }

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );

            } else {
                last_error = GetLastError();

                if (last_error == ERROR_SEM_TIMEOUT) {
                    // check if dlls and threads have been unloaded
                    status = unload_dlls_and_threads_.wait_for(0ms);
                } else {
                    printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                    globals::running = false;
                    return false;
                }
            }                
        }
        return true;  
    }

    void TimeRestorePhase::Exit() {
        RestoreMemoryAtSequence(target_run_id_, target_run_sequence_);
        
        // Once we've restored now we start a "new" run 
        // TODO: This may need to be moved into the different "playing" phases because this will always increment
        //  even if we're just jumping around from snapshot to snapshot
        globals::run_id += 1;
        globals::run_sequence = 0;
        
        CreateThreadHistoryBranch(target_run_id_, target_run_sequence_, globals::run_id);
        CreateModuleHistoryBranch(target_run_id_, target_run_sequence_, globals::run_id);
        return;
    }
}