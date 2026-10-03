#pragma once
#include "argparse/argparse.hpp"
#include "cli_command.h"
#include "transition_variant.h"
namespace chronoporia {

    class SnapshotCommand : public CLICommand {
    public:
        SnapshotCommand() {};
        SnapshotCommand(argparse::ArgumentParser& debugger_parser) 
            : snapshot_command {"snapshot", "1.0", argparse::default_arguments::all, false} 
        {
            snapshot_command.add_description("List snapshots or restore to a specified snapshot.");
            snapshot_command.add_argument("--list").flag();
            snapshot_command.add_argument("--run_id").default_value(std::string{});
            snapshot_command.add_argument("--run_seq").default_value(std::string{});
            debugger_parser.add_subparser(snapshot_command);
        }

        std::optional<Transitions> handle_command();
    private:
        std::optional<Transitions> handle_run_id_seq();

        argparse::ArgumentParser snapshot_command;
    };
}