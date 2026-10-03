#pragma once
#include "argparse/argparse.hpp"
#include "cli_command.h"
#include "transition_variant.h"
namespace chronoporia {

    class DetachCommand : public CLICommand {
    public:
        DetachCommand() {};
        DetachCommand(argparse::ArgumentParser& debugger_parser) 
            : detach_command {"detach", "1.0", argparse::default_arguments::all, false} 
        {
            detach_command.add_description("Detach chronoporia from the child process.  Call 'resume' after to resume the child process.");
            debugger_parser.add_subparser(detach_command);
        }

        std::optional<Transitions> handle_command();
    private:
        argparse::ArgumentParser detach_command;
    };
}