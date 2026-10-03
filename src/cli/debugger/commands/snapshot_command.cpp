#include "argparse/argparse.hpp"
#include "snapshot_command.h"
#include "snapshot_log.h"
#include "transition_variant.h"
#include <optional>

namespace chronoporia {

    std::optional<Transitions> SnapshotCommand::handle_command() {
        if (snapshot_command.get<bool>("--list")) {
            PrintSnapshotHistory();
            return std::nullopt;
        }

        if (snapshot_command.is_used("--run_id") || snapshot_command.is_used("--run_seq")) {
            return handle_run_id_seq();
        }

        printf("%s\n", snapshot_command.help().str().c_str());
        return std::nullopt;
    }

    std::optional<Transitions> SnapshotCommand::handle_run_id_seq() {
        auto run_id_str  = snapshot_command.get<std::string>("--run_id");
        auto run_seq_str = snapshot_command.get<std::string>("--run_seq");

        if (run_id_str.empty() || run_seq_str.empty()) {
            printf("Please supply both a run_id and run_seq for the snapshot\n");
            return std::nullopt;
        }

        uint32_t run_id  = static_cast<uint32_t>(std::stoul(run_id_str));
        uint32_t run_seq = static_cast<uint32_t>(std::stoul(run_seq_str));

        if (!ValidSnapshotIdSeq(run_id, run_seq)) {
            printf("Please supply a valid run_id and run_seq for the snapshot.  Run snapshot --list to see available snapshots\n");
            return std::nullopt;
        }

        // guaranteed by the valid check above
        auto target_global_sequence = GetGlobalSequence(run_id, run_seq);
        // TODO: Fix this later with a better way to transition back to the debugger
        return TransitionToTimeRestore {{true}, run_id, run_seq, target_global_sequence.value(),TransitionsBox{Transitions{TransitionToDebugger {true}}}};
    }
}