#include "process_start_phase.h"
#include "error_transition.h"
#include "globals.h"
#include "nt_wrappers.h"
#include "breakpoint_manager.h"
#include "quill/LogMacros.h"
#include "record_transition.h"

namespace chronoporia {

    void ProcessStartPhase::Enter() {

    };

    Transitions ProcessStartPhase::Run() {
        DEBUG_EVENT de;
        bool debug_event_success;

        while (globals::running) {
            debug_event_success = WaitForDebugEvent(&de, INFINITE);

            if (debug_event_success) {
                DWORD continue_status = DBG_CONTINUE;

                if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                    const EXCEPTION_RECORD& er = de.u.Exception.ExceptionRecord;

                    if (er.ExceptionCode == STATUS_BREAKPOINT) {
                        // Can only set the child breakpoint once the windows debug breakpoint has been sent
                        // This guarantees all necessary dlls (including child process) for launch have been initialized 
                        if (!process_loaded) {
                            process_loaded = true;
                            SuspendProcess();
                            ContinueDebugEvent(
                                de.dwProcessId,
                                de.dwThreadId,
                                continue_status
                            );
                            globals::main_thread_id = de.dwThreadId;
                            return TransitionToRecording {true};
                        };
                    };
                };

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );
            } else {
                DWORD last_error = GetLastError();
                LOG_ERROR(globals::logger, "Unknown error encountered from WaitDebugEvent {}", last_error);
                return TransitionToError {false, last_error};
            }
        }               
    }

    // Now that we know the child process is fully setup, place a breakpoint at the entry address 
    void ProcessStartPhase::Exit() {
        uintptr_t child_entry_address = GetChildEntryAddress();
        CreateReturnBreakpoint(child_entry_address, globals::main_thread_id);
    }
}
