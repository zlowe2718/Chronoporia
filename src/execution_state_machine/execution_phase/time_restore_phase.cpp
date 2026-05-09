#include "time_restore_phase.h"
#include "error_transition.h"
#include "shellcode.h"
#include "breakpoint_manager.h"
#include "module_manager.h"
#include "thread_manager.h"
#include "memory_manager.h"
#include "globals.h"
#include "thread_utils.h"
#include "nt_wrappers.h"
#include "transition.h"
#include <cstdint>
#include <errhandlingapi.h>
#include <future>
#include <minwinbase.h>
#include <winbase.h>

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

namespace chronoporia {
    // TODO: reenable shell code execution.  Order should be dll -> threads -> memory
    // During the dll and thread fixing we'll need to be running WaitForDebugEvent so the process
    // Can proceed with unloading/loading dlls and destroying/creating threads
    void TimeRestorePhase::Enter() {
        auto dlls_to_unload = RestoreDLLsAtSequence(target_sequence);

        code_size = AddDllUnload(dlls_to_unload);
        code_address = WriteShellCodeBufferToProcess();

        TrackShellCodeBreakpoint(code_address + code_size - 1);

        // Thread hijacking to run shell code
        HANDLE hThread = GetThreadHandle(globals::main_thread_id);
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(hThread, &ctx);

        ctx.Rip = code_address;
        // Align RSP to 16 bytes — the sub rsp, 0x28 in the
        // shellcode will maintain alignment from here
        ctx.Rsp = ctx.Rsp & ~0xFULL;
        SetThreadContext(hThread, &ctx);

    }

    Transition TimeRestorePhase::Run() {
        if (!RunDllRestore()) {
            return TransitionToError {GetLastError()};
        }
        if (!RunThreadRestore()) {
            return TransitionToError {GetLastError()};
        }
        return TransitionToPlayback {};
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

                        if (er.ExceptionCode == STATUS_BREAKPOINT) {
                            SuspendThreadId(globals::main_thread_id);
                            ContinueDebugEvent(
                                de.dwProcessId,
                                de.dwThreadId,
                                continue_status
                            );
                            // TODO: do an explicit check for the correct shellcode breakpoint
                            return true;
                        }            
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

        unload_dlls_and_threads = std::async(std::launch::async, [this]() {
            RestoreThreadsAtSequence(target_sequence);
        });
        std::future_status status = unload_dlls_and_threads.wait_for(0ms);
        while (status != std::future_status::ready) {
            // Do I need to resume the process here?
            debug_event_success = WaitForDebugEvent(&de, 0);

            if (debug_event_success) {
                DWORD continue_status = DBG_CONTINUE;

                switch (de.dwDebugEventCode) {
                    case CREATE_THREAD_DEBUG_EVENT:
                        printf("Thread created\n");
                        break;
                    case EXIT_THREAD_DEBUG_EVENT:
                        printf("Thread exiting\n");
                        break;
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
                    status = unload_dlls_and_threads.wait_for(0ms);
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
        RestoreMemoryAtSequence(target_sequence);
        ResumeProcess();
        return;
    }
}