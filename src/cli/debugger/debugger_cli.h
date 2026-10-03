#pragma once
#include "commands/detach_command.h"
#include "commands/resume_command.h"
#include "commands/step_command.h"
#include "dump_command.h"
#include "breakpoint_command.h"
#include "snapshot_command.h"
#include "transition_variant.h"
#include <argparse/argparse.hpp>

namespace chronoporia {

    class DebuggerCLI {
    public:
        DebuggerCLI()
            : debugger_parser_ {"debugger", "1.0", argparse::default_arguments::all, false}
            , dump_command_ {debugger_parser_}
            , breakpoint_command_ {debugger_parser_}
            , snapshot_command_ {debugger_parser_}
            , detach_command_ {debugger_parser_}
            , resume_command_ {debugger_parser_}
            , step_command_ {debugger_parser_}
        {
        }

        std::optional<Transitions> handle_cli(const std::vector<std::string> &args);
    private:
        argparse::ArgumentParser debugger_parser_;
        DumpCommand dump_command_;
        BreakpointCommand breakpoint_command_;
        SnapshotCommand snapshot_command_;
        DetachCommand detach_command_;
        ResumeCommand resume_command_;
        StepCommand step_command_;
    };
}