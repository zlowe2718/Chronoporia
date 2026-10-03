#include "resume_command.h"
#include "breakpoint_manager.h"
#include "nt_wrappers.h"
#include "plugin_manager.h"
#include "thread_utils.h"
#include "trampoline.h"

namespace {
    // Set when a user breakpoint stops the program (ResumeProgram returns nullopt) so that
    // the next call to ResumeProgram knows which breakpoint/thread needs to be stepped over
    // before execution can continue past it.
    // Can't place this in the class since it gets reset every resume command
    std::optional<std::pair<uintptr_t, DWORD>> pending_user_breakpoint_;    
}

namespace chronoporia {

    std::optional<Transitions> ResumeCommand::handle_command() {
        // thin wrapper around resume process in case we want to handle other commands later
        return resume_process_execution();
    }

    std::optional<Transitions> ResumeCommand::resume_process_execution() {
        DEBUG_EVENT de;
        bool debug_event_success;

        if (pending_user_breakpoint_) {
            auto [address, thread_id] = *pending_user_breakpoint_;
            pending_user_breakpoint_.reset();

            // Lift the breakpoint just long enough for the owning thread to step over it,
            // keeping every other thread frozen until the trap fires.
            disable_user_breakpoint(address);
            SuspendAllThreadsExceptCurrent(thread_id);
            SetTrapFlag(thread_id);
            ResumeProcess();
            single_step_and_restore(address, thread_id);
        } else {
            ResumeProcess();
        }

        while (globals::running) {
            debug_event_success = WaitForDebugEvent(&de, INFINITE);

            if (debug_event_success) {
                DWORD continue_status = DBG_CONTINUE;

                if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                    const EXCEPTION_RECORD& er = de.u.Exception.ExceptionRecord;

                    if (er.ExceptionCode == STATUS_BREAKPOINT) {
                        DWORD thread_id = de.dwThreadId;
                        uintptr_t rip_address = GetRipAddress(thread_id) - 1;
                        auto bp_type = GetBreakpointType(rip_address, thread_id);

                        switch (bp_type.value()) {
                            // Permanent breakpoints just need to run the trampoline and continue
                            case BreakpointType::Permanent: {
                                RedirectToTrampoline(rip_address, thread_id);
                                break;
                            }
                            case BreakpointType::User: {
                                RollBackInstructionPointRegister(thread_id);

                                bool should_break = true;
                                if (is_user_breakpoint_conditional(rip_address)) {
                                    should_break = reached_breakpoint("cpp", rip_address, thread_id);
                                }

                                if (should_break) {
                                    // Leave the breakpoint in place and remember it so the next
                                    // resume steps over it before continuing normally.
                                    pending_user_breakpoint_ = {rip_address, thread_id};

                                    SuspendProcess();
                                    ContinueDebugEvent(
                                        de.dwProcessId,
                                        de.dwThreadId,
                                        continue_status
                                    );
                                    return std::nullopt;
                                }

                                // Condition wasn't satisfied: step over the breakpoint
                                // ourselves and keep the program running.
                                disable_user_breakpoint(rip_address);
                                SuspendAllThreadsExceptCurrent(thread_id);
                                SetTrapFlag(thread_id);
                                ContinueDebugEvent(
                                    de.dwProcessId,
                                    de.dwThreadId,
                                    continue_status
                                );
                                single_step_and_restore(rip_address, thread_id);
                                continue;
                            }
                            default:
                                break;
                        }
                    };
                };

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );
            } else {
                DWORD last_error = GetLastError();
                printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                return TransitionToError {false, last_error};
            }
        }
        return TransitionToError {false, 0};
    }

    // Suspends every other thread, single-steps thread_id past address, restores the
    // breakpoint byte, then resumes everyone else. address must already have its original
    // byte restored and the trap flag set on thread_id before this is called; the thread's
    // triggering debug event must already have been continued so it is free to run.
    void ResumeCommand::single_step_and_restore(uintptr_t address, DWORD thread_id) {
        DEBUG_EVENT de {};
        bool got_single_step = false;

        while (globals::running && !got_single_step) {
            if (!WaitForDebugEvent(&de, INFINITE)) break;

            if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP &&
                de.dwThreadId == thread_id) {
                got_single_step = true;
                break;
            }

            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }

        enable_user_breakpoint(address);

        if (got_single_step) {
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }

        ResumeAllThreads();
    }

}