#include "step_command.h"
#include "base_breakpoint_plugin.h"
#include "breakpoint_manager.h"
#include "globals.h"
#include "nt_wrappers.h"
#include "quill/LogMacros.h"
#include "reverse_replay_transition.h"
#include "thread_utils.h"
#include "plugin_manager.h"
#include "snapshot_log.h"
#include <algorithm>
#include <debugapi.h>
#include <minwinbase.h>
#include <optional>
#include <unordered_map>
#include <winnt.h>

namespace chronoporia {

    // TODO: I need to be careful of already placed breakpoints and trampolines when reading memory
    // to find information like branch points and when resuming execution

    // Change into out etc into a position command called target?  This way I can have stuff like --thread to target a thread specifically
    // TODO: I want different commands for stepping.  Due to multiple threads I want a:
    //  --first -> stop on first thread
    //  --thread -> only step for specified thread.  leave all other threads frozen
    //  --all -> step all threads.
    std::optional<Transitions> StepCommand::handle_command() {
        uint64_t thread_counter = read_thread_tls(globals::main_thread_id, TlsKind::BranchCounter);
        if (step_command.get<bool>("--into")) {
            step_source(globals::main_thread_id, breakpoint_plugin::StepMode::Into);
            return std::nullopt;
        }
        if (step_command.get<bool>("--out")) {
            step_out_from(globals::main_thread_id);
            return std::nullopt;
        }
        if (step_command.get<bool>("--line")) {
            step_source(globals::main_thread_id, breakpoint_plugin::StepMode::Line);
            return std::nullopt;
        }
        if (step_command.get<bool>("--instruction")) {
            SetTrapFlag(globals::main_thread_id);
            step_instruction(globals::main_thread_id);
            return std::nullopt;
        }

        if (step_command.get<bool>("--reverse-instruction")) {
            // TODO: need to account if the rip has been rolled back or not
            //  no -1 for now since I'm running from the resume command which is already rolled back
            const uintptr_t current_address = GetRipAddress(globals::main_thread_id);
            const auto& predecessors = get_address_predecesors("cpp", current_address, breakpoint_plugin::ReverseStepMode::Instruction, globals::main_thread_id);
            return transition_to_time_restore(predecessors, current_address, thread_counter);
        }

        if (step_command.get<bool>("--reverse-line")) {
            const uintptr_t current_address = GetRipAddress(globals::main_thread_id);
            const auto& predecessors = get_address_predecesors("cpp", current_address, breakpoint_plugin::ReverseStepMode::Line, globals::main_thread_id);
            return transition_to_time_restore(predecessors, current_address, thread_counter);
        }

        if (step_command.get<bool>("--reverse-into")) {
            const uintptr_t current_address = GetRipAddress(globals::main_thread_id);
            const auto& predecessors = get_address_predecesors("cpp", current_address, breakpoint_plugin::ReverseStepMode::Into, globals::main_thread_id);
            return transition_to_time_restore(predecessors, current_address, thread_counter);
        }

        if (step_command.get<bool>("--reverse-out")) {
            // TODO: get out predecessors -> place breakpoint at frame return address
            // TODO: update get frame return to take in a "has_symbols" paramater that walks until a return address with symbols is found
            const uintptr_t current_address = GetRipAddress(globals::main_thread_id);
            const auto frame_return = get_frame_return_address("cpp", globals::main_thread_id);
            const auto true_frame_return = get_true_address("cpp", frame_return);
            std::vector<breakpoint_plugin::PotentialPredecessor> predecessor = {{true_frame_return, thread_counter, false}};
            return transition_to_time_restore(predecessor, current_address, thread_counter);
        }

        printf("%s\n", step_command.help().str().c_str());
        return std::nullopt;
    }

    Transitions StepCommand::transition_to_time_restore(const std::vector<breakpoint_plugin::PotentialPredecessor>& predecessors, const uintptr_t current_address, const uint64_t current_branch_counter) {
        uint64_t branch_target = globals::kTargetSentinel;
        std::vector<breakpoint_plugin::ReturnAddress> frame_return_addresses;
        // use branch target fallback
        if (predecessors.size() == 0) {
            branch_target = get_branch_target("cpp", current_address, globals::main_thread_id);
            frame_return_addresses = get_all_frame_return_addresses("cpp", globals::main_thread_id);
        }
        const auto nearest_snapshot = get_nearest_snapshot();
        TransitionToReverseReplay reverse_transition {
            true, 
            std::move(predecessors), 
            std::move(frame_return_addresses), 
            globals::main_thread_id, 
            branch_target,
            current_address,
            current_branch_counter,
        };
        return TransitionToTimeRestore {true, nearest_snapshot->run_id, nearest_snapshot->run_sequence, nearest_snapshot->global_sequence, TransitionsBox{Transitions{reverse_transition}}};
    }

    void StepCommand::step_instruction(const DWORD thread_id, const bool this_thread_only) {
        DEBUG_EVENT de {};

        if (this_thread_only) {
            ResumeThreadId(thread_id);
        } else {
            ResumeProcess();
        }
        while (globals::running) {
            if (!WaitForDebugEvent(&de, INFINITE)) break;

            if (
                de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP &&
                de.dwThreadId == thread_id
            ) {
                if (this_thread_only) {
                    SuspendThreadId(thread_id);
                } else {
                    SuspendProcess();
                }
                ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                break;
            } else if (
                de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
                de.dwThreadId == thread_id                
            ) {
                // Ran into a breakpoint, rollback and stop at the breakpoint
                if (this_thread_only) {
                    SuspendThreadId(thread_id);
                } else {
                    SuspendProcess();
                }
                RollBackInstructionPointRegister(thread_id);
                ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                break;
            }

            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }
    }

    void StepCommand::step_out_from(const DWORD thread_id, const bool this_thread_only) {
        uintptr_t return_address = get_frame_return_address("c++", thread_id);
        run_to_address(return_address, thread_id, this_thread_only);
    }

    void StepCommand::step_source(const DWORD thread_id, const breakpoint_plugin::StepMode step_mode, const bool this_thread_only) {
        // TODO: all breakpoint stopped at USER need to be evaluated if conditional
        const std::string& plugin_language = globals::current_debugger_language;

        CONTEXT ctx = GetThreadContextFromId(thread_id);
        const uintptr_t initial_stack_pointer = ctx.Rsp;
        uintptr_t current_address = ctx.Rip;

        uintptr_t original_address = get_original_address(plugin_language, current_address);
        const auto initial_source = get_source_info_for_address(plugin_language, original_address);

        // TODO: If no source information then place breakpoint at nearest frame pointer that does have source information
        if (initial_source.line_number == 0) {
            LOG_WARNING(globals::logger, "No line information at 0x{:x}", ctx.Rip);
            return;
        }

        const auto bp_to_place = get_breakpoints_for_step(plugin_language, original_address, step_mode);

        // Here we're already rolled back so we can disable and replay the instruction
        // Don't need to roll back since this uses a trap flag
        if (GetBreakpointType(current_address, thread_id) == BreakpointType::User) {
            single_step_user_breakpoint(current_address, thread_id);
        }

        // Run until we hit a breakpoint
        const auto stopped_at_breakpoint = run_step_mode(thread_id, bp_to_place, initial_stack_pointer);
        if(!stopped_at_breakpoint) {
            // We hit a user breakpoint, just return
            return;
        }

        // TODO: separate switch case to handle missing source info
        if (!stopped_at_breakpoint->has_source_info) {
            LOG_WARNING(globals::logger, "No Source information at stopped address {:p}", stopped_at_breakpoint->true_address);
            return;
        }

        switch (stopped_at_breakpoint->step_kind) {
            case breakpoint_plugin::StepBreakpointKind::Line: {
                // easiest case, just return
                return;
            }
            case breakpoint_plugin::StepBreakpointKind::Return: {
                // second easiest, just single step then return
                SetTrapFlag(globals::main_thread_id);
                step_instruction(globals::main_thread_id);
                return;
            }
            case breakpoint_plugin::StepBreakpointKind::Call: {
                // leaving this as separate from Return in case we want to skip the prologue later
                SetTrapFlag(globals::main_thread_id);
                step_instruction(globals::main_thread_id);
                return;
            }
            case breakpoint_plugin::StepBreakpointKind::IndirectJump: {
                // TODO: we need to verify this isn't the same line.  If it jumped back to the same line
                // then replan
                SetTrapFlag(globals::main_thread_id);
                step_instruction(globals::main_thread_id);
                return;
            }
        }
    }

    bool StepCommand::run_to_address(uintptr_t address, DWORD thread_id, const uintptr_t stack_pointer, const bool this_thread_only) {
        CreateReturnBreakpoint(address, thread_id);
        
        std::vector<DWORD> suspended_thread_ids {};
        bool complete = false;

        if (this_thread_only) {
            ResumeThreadId(thread_id);
        } else {
            ResumeProcess();
        }

        DEBUG_EVENT de{};
        while (globals::running) {
            if (!WaitForDebugEvent(&de, INFINITE)) break;

            const bool breakpoint_reached =                 
                de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
                GetRipAddress(de.dwThreadId) - 1 == address;

            if (breakpoint_reached && de.dwThreadId == thread_id) {
                RollBackInstructionPointRegister(thread_id);

                // recursion check, if current rsp is less than the current stack pointer then continue until we've fully unwound
                if (GetThreadContextFromId(thread_id).Rsp <= stack_pointer) {
                    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                    continue;
                }

                ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                if (this_thread_only) {
                    SuspendThreadId(thread_id);
                } else {
                    SuspendProcess();
                }
                complete = true;
                break;
            } else if (breakpoint_reached) {
                // If not the thread we're looking for then suspend it
                RollBackInstructionPointRegister(de.dwThreadId);
                SuspendThreadId(de.dwThreadId);
                suspended_thread_ids.push_back(de.dwThreadId);                
            }
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }

        RemoveBreakpoint(address, thread_id);
        // This is safe because if suspended thread ids has ids then we will have already called SuspendProcess
        for (const auto thread : suspended_thread_ids) {
            ResumeThreadId(thread);
        }
        
        return complete;
    }

    std::optional<breakpoint_plugin::StepBreakpoint> StepCommand::run_step_mode(const DWORD thread_id, const std::vector<breakpoint_plugin::StepBreakpoint> potential_stops, const uintptr_t stack_pointer) {
        std::vector<DWORD> suspended_thread_ids {};
        std::optional<breakpoint_plugin::StepBreakpoint> stopped_at_breakpoint = std::nullopt;

        std::unordered_map<uintptr_t, breakpoint_plugin::StepBreakpointKind> breakpoint_to_kind;
        for (const auto& step_breakpoint : potential_stops) {
            breakpoint_to_kind[step_breakpoint.true_address] = step_breakpoint.step_kind;
            create_step_breakpoint(step_breakpoint.true_address, thread_id);
        }

        ResumeThreadId(thread_id);
        bool hit_breakpoint = false;
        DEBUG_EVENT de{};
        while (globals::running && !hit_breakpoint) {
            if (!WaitForDebugEvent(&de, INFINITE)) break;

            if (de.dwDebugEventCode != EXCEPTION_DEBUG_EVENT || de.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) {
                if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
                    LOG_ERROR(globals::logger, "Error encountered when running step");
                    break;
                }
                ContinueDebugEvent(de.dwProcessId,de.dwThreadId,DBG_CONTINUE);                
                break;
            }      

            const DWORD current_thread_id = de.dwThreadId;
            const uintptr_t current_address = GetRipAddress(current_thread_id) - 1;
            const auto debugger_breakpoint_kind = GetBreakpointType(current_address, current_thread_id);

            switch (*debugger_breakpoint_kind) {
                case BreakpointType::Permanent: {
                    // TODO: We need to determine if we need to replay the event or redirect to trampoline since theoretically we could be in either situation
                    break;
                }
                case BreakpointType::User: {
                    RollBackInstructionPointRegister(thread_id);

                    bool should_break = true;
                    // user breakpoints hit first in GetBreakpointType, if this was a step target line then we need to skip this
                    if (!is_breakpoint_type_step(current_address, current_thread_id) &&
                        is_user_breakpoint_conditional(current_address)
                    ) {
                        // if breakpoint not reached or then we need to continue
                        if (!reached_breakpoint("cpp", current_address, thread_id)) {
                            SuspendThreadId(current_thread_id);
                            ContinueDebugEvent(
                                de.dwProcessId,
                                de.dwThreadId,
                                DBG_CONTINUE
                            );
                            single_step_user_breakpoint(current_address, thread_id);
                            // Single step ends with resuspending the thread so we need to resume it to continue here
                            ResumeThreadId(current_thread_id);
                            continue;
                        }
                    }

                    // If user breakpoint then we can early exit since this should have a higher priority
                    hit_breakpoint = true;
                    SuspendThreadId(current_thread_id);
                    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                    continue;
                }
                case BreakpointType::Return: {
                    // TODO: We need to determine if we need to replay the event or redirect to trampoline since theoretically we could be in either situation
                    break;
                }
                case BreakpointType::Step: {
                    // Only need to check this in Step since the rest of the program should be running like normal
                    if (current_thread_id != thread_id) {
                        // If not the thread we're looking for then suspend it
                        RollBackInstructionPointRegister(current_thread_id);
                        SuspendThreadId(current_thread_id);
                        suspended_thread_ids.push_back(current_thread_id);
                        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
                        continue;
                    }

                    hit_breakpoint = true;
                    const auto itr = std::find_if(potential_stops.begin(), potential_stops.end(), 
                        [current_address](const auto& stop) {
                            return stop.true_address == current_address;
                        }
                    );
                    if (itr == potential_stops.end()) {
                        LOG_ERROR(globals::logger, "Couldn't find step breakpoint from address {:x}", current_address);
                    }
                    stopped_at_breakpoint = *itr;
                    RollBackInstructionPointRegister(current_thread_id);
                    SuspendThreadId(current_thread_id);
                    break;
                }
            }

            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }
        
        for (const auto& step_breakpoint : potential_stops) {
            RemoveBreakpoint(step_breakpoint.true_address, thread_id);
        }
        return stopped_at_breakpoint;
    }

    void StepCommand::single_step_user_breakpoint(uintptr_t address, DWORD thread_id) {
        disable_user_breakpoint(address);
        SetTrapFlag(thread_id);
        ResumeThreadId(thread_id);
        
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
            SuspendThreadId(thread_id);
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }
    }

}