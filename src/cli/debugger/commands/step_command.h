#pragma once
#include "argparse/argparse.hpp"
#include "cli_command.h"
#include "transition_variant.h"
#include "base_breakpoint_plugin.h"
namespace chronoporia {

    class StepCommand : public CLICommand {
    public:
        StepCommand() {};
        StepCommand(argparse::ArgumentParser& debugger_parser) 
        : step_command {"step", "1.0", argparse::default_arguments::all, false} 
        {
            step_command.add_description("Step into, out from, and to next line or instruction.");
            step_command.add_argument("--into").flag();
            step_command.add_argument("--out").flag();
            step_command.add_argument("--line").flag();
            step_command.add_argument("--instruction").flag();
            step_command.add_argument("--reverse-instruction").flag();
            step_command.add_argument("--reverse-line").flag();
            step_command.add_argument("--reverse-into").flag();
            step_command.add_argument("--reverse-out").flag();
            debugger_parser.add_subparser(step_command);
        }

        std::optional<Transitions> handle_command();
    private:
        void step_out_from(const DWORD thread_id, const bool this_thread_only = false);
        void step_source(const DWORD thread_id, const breakpoint_plugin::StepMode step_mode, const bool this_thread_only = false);
        void step_instruction(const DWORD thread_id, const bool this_thread_only = false);
        bool run_to_address(const uintptr_t address, const DWORD thread_id, const uintptr_t stack_pointer, const bool this_thread_only = false);
        void single_step_user_breakpoint(uintptr_t address, DWORD thread_id);
        std::optional<breakpoint_plugin::StepBreakpoint> run_step_mode(const DWORD thread_id, const std::vector<breakpoint_plugin::StepBreakpoint> potential_stops, const uintptr_t stack_pointer);
        Transitions transition_to_time_restore(const std::vector<breakpoint_plugin::PotentialPredecessor>& predecessors, const uintptr_t current_address, const uint64_t current_branch_counter);
        argparse::ArgumentParser step_command;
    };
}