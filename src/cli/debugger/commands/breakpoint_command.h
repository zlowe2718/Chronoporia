#pragma once
#include "argparse/argparse.hpp"
#include "cli_command.h"
#include "transition_variant.h"
namespace chronoporia {

    class BreakpointCommand : public CLICommand {
    public:
        BreakpointCommand() {};
        BreakpointCommand(argparse::ArgumentParser& debugger_parser) 
            : breakpoint_command {"bp", "1.0", argparse::default_arguments::all, false} 
        {
            breakpoint_command.add_description("Add or remove breakpoints and conditional breakpoints.");
            breakpoint_command.add_argument("--list").flag();
            breakpoint_command.add_argument("--remove").default_value(std::string{});
            breakpoint_command.add_argument("--address").default_value(std::string{});
            breakpoint_command.add_argument("--function").default_value(std::string{});
            breakpoint_command.add_argument("--file").default_value(std::string{});
            breakpoint_command.add_argument("--line").default_value(std::string{});
            breakpoint_command.add_argument("--condition").default_value(std::string{});
            debugger_parser.add_subparser(breakpoint_command);
        }

        std::optional<Transitions> handle_command();
    private:

        argparse::ArgumentParser breakpoint_command;
    };

}