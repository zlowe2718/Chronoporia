#pragma once
#include "argparse/argparse.hpp"
#include "cli_command.h"
#include "transition_variant.h"

namespace chronoporia {

    class ResumeCommand : public CLICommand {
    public:
        ResumeCommand() {};
        ResumeCommand(argparse::ArgumentParser& debugger_parser) 
            : resume_command {"resume", "1.0", argparse::default_arguments::all, false} 
        {
            resume_command.add_description("Resume execution of the child process.");
            debugger_parser.add_subparser(resume_command);
        }

        std::optional<Transitions> handle_command();
    private:

        std::optional<Transitions> resume_process_execution();
        void single_step_and_restore(uintptr_t address, DWORD thread_id);

        argparse::ArgumentParser resume_command;
    };
}