#include "debugger_transition.h"
#include "nt_wrappers.h"
#include "reverse_replay_phase.h"
#include "snapshot_log.h"
#include "thread_utils.h"
#include "trampoline.h"
#include "transition_variant.h"
#include "globals.h"
#include "breakpoint_manager.h"
#include "base_breakpoint_plugin.h"
#include "quill/LogMacros.h"
#include <optional>

namespace {
    using namespace chronoporia;

    void single_step_and_restore(uintptr_t address, DWORD thread_id) {
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

        enable_breakpoint(address);

        if (got_single_step) {
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }

        ResumeAllThreads();
    }
}


namespace chronoporia {

    void ReverseReplayPhase::Enter() {
        for (const auto& predecessor : predecessors_) {
            create_reverse_breakpoint(predecessor.true_address, replay_thread_id_);
        }
        if (branch_target_ != globals::kTargetSentinel) {
            // Need to set the reverse catcher manually after the branch target was triggered here
            update_thread_tls(replay_thread_id_, TlsKind::BranchTarget, branch_target_);
        } else {
            // TODO: If either of these hit then follow the fallback instructions for the type of reverse step we're doing
            // probably need a reverse state machine ( so support composite state machines)
            reverse_catchers_ = ReverseCatchers {stopped_address_, stopped_branch_counter_, stopped_branch_counter_ + 1};
            create_reverse_breakpoint(stopped_address_, replay_thread_id_);
            update_thread_tls(replay_thread_id_, TlsKind::BranchTarget, stopped_branch_counter_ + 1);
        }
    }

    // TODO: probably state machines to handle the differnt "we didnt find it" situations,
    // some will need two replays, some need to go back farther, some need three replays etc.

    // TODO: For now I'm running the save the last found branch counter for the address and replay to that fallback
    Transitions ReverseReplayPhase::Run() {
        // TODO: handle if instruction has no branch counter bound predecessor like (while true {(*x)++})
        DEBUG_EVENT de{};
        ResumeProcess();
        while (globals::running) {
            if (!WaitForDebugEvent(&de, INFINITE)) break;

            if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {

                const DWORD thread_id = de.dwThreadId;
                if (thread_id != replay_thread_id_) {
                    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                    continue;
                };

                const uint64_t tls_branch_counter = read_thread_tls(thread_id, TlsKind::BranchCounter);
                const uintptr_t current_address = GetRipAddress(thread_id) - 1;

                // We didn't find what we were looking for, try again
                if (
                    (current_address == reverse_catchers_.catch_address && tls_branch_counter == reverse_catchers_.catch_branch_counter) 
                    || tls_branch_counter >= stopped_branch_counter_
                ) {
                    const auto nearest_snapshot = get_nearest_snapshot();
                    // update predecessors with the last seen branch counter and retry
                    for (auto& predecessor : predecessors_) {
                        if (pred_last_seen_ctr_.contains(predecessor.true_address)) {
                            predecessor.branch_counter = pred_last_seen_ctr_[predecessor.true_address];
                        }
                    }

                    SuspendProcess();
                    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);

                    TransitionToReverseReplay reverse_transition {
                        true, 
                        std::move(predecessors_), 
                        std::move(frame_return_addresses_), 
                        globals::main_thread_id, 
                        branch_target_,
                        stopped_address_,
                        stopped_branch_counter_,
                    };
                    return TransitionToTimeRestore {true, nearest_snapshot->run_id, nearest_snapshot->run_sequence, nearest_snapshot->global_sequence, TransitionsBox{Transitions{reverse_transition}}};
                }

                // if we're at the fallback catch address and its not the branch coutner we expect then just continue;
                if (current_address == reverse_catchers_.catch_address) {
                    RollBackInstructionPointRegister(thread_id);
                    disable_breakpoint(current_address);
                    SuspendAllThreadsExceptCurrent(thread_id);
                    SetTrapFlag(thread_id);                 
                    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                    single_step_and_restore(current_address, thread_id);
                    continue;         
                }

                const auto predecessor = get_potential_predecessor(current_address);

                if (predecessor) {
                    RollBackInstructionPointRegister(thread_id);
                    if (tls_branch_counter == predecessor->branch_counter) {
                        // found the predecessor
                        SuspendProcess();
                        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                        return TransitionToDebugger {true};
                    }

                    pred_last_seen_ctr_[current_address] = tls_branch_counter;
                    disable_breakpoint(current_address);
                    SuspendAllThreadsExceptCurrent(thread_id);
                    SetTrapFlag(thread_id);                 
                    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                    single_step_and_restore(current_address, thread_id);
                    continue;
                }

                // Some other (permanent/instrumentation) breakpoint -- service it.
                // TODO: ignore the other breakpoints or disable them in enter and enable in exit?
                if (GetBreakpointType(current_address, thread_id) == BreakpointType::Permanent)
                    RedirectToTrampoline(current_address, thread_id);
            }

            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }

        // WaitForDebugEvent Errored
        DWORD last_error = GetLastError();
        LOG_ERROR(globals::logger, "Unknown error encountered from WaitDebugEvent {}", last_error);
        globals::running = false;
        return TransitionToError {false, last_error};
    }

    void ReverseReplayPhase::Exit() {
        for (const auto& predecessor : predecessors_) {
            RemoveBreakpoint(predecessor.true_address, replay_thread_id_);
        }
        if (branch_target_ != globals::kTargetSentinel) {
            update_thread_tls(replay_thread_id_, TlsKind::BranchTarget, globals::kTargetSentinel);
        }
        if (reverse_catchers_.catch_address != 0) {
            RemoveBreakpoint(reverse_catchers_.catch_address, replay_thread_id_);
            update_thread_tls(replay_thread_id_, TlsKind::BranchTarget, globals::kTargetSentinel);
        }
    }

    std::optional<breakpoint_plugin::PotentialPredecessor> ReverseReplayPhase::get_potential_predecessor(const uintptr_t address) {
        for (const auto& predecessor : predecessors_) {
            if (predecessor.true_address == address) return predecessor;
        }
        return std::nullopt;
    }
}