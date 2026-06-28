#include "snapshot_log.h"
#include "memory_manager.h"
#include "quill/LogMacros.h"
#include "thread_manager.h"
#include "execution_tree.h"
#include "globals.h"
#include <string>

namespace {
    chronoporia::ExecutionTree<chronoporia::Snapshot> snapshot_history {{}, 0, 0, 0};

    constexpr std::string to_string(chronoporia::SnapshotType snapshot_type) {
        switch (snapshot_type) {
            case chronoporia::SnapshotType::CoarseSnapshot:   return "Coarse Snapshot";
            case chronoporia::SnapshotType::MicroSnapshot:    return "Micro Snapshot";
            case chronoporia::SnapshotType::LineSnapshot:     return "Line Snapshot";
            default:           return "Unknown";
        }
    }

}


namespace chronoporia {

    // TODO: is there a better way to do this because I don't want an empty ExecutionTree?
    void StartSnapshotHistory() {
        Snapshot snap { SnapshotType::CoarseSnapshot, globals::run_id, globals::run_sequence, globals::global_sequence };
        snapshot_history = ExecutionTree<Snapshot> {snap, globals::run_id, globals::run_sequence, globals::global_sequence};
    }

    void SnapshotProcess(SnapshotType snapshot_type) {
        LOG_INFO(globals::logger, "--- Taking {} ---", to_string(snapshot_type));

        Snapshot new_snapshot {snapshot_type, globals::run_id, globals::run_sequence, globals::global_sequence};

        SnapshotThreads(globals::global_sequence, globals::run_id, globals::run_sequence);
        SnapshotMemory(globals::global_sequence, globals::run_id, globals::run_sequence);

        snapshot_history.AddChild(std::move(new_snapshot), globals::run_id, globals::run_sequence, globals::global_sequence);

        globals::run_sequence += 1;
    }

    void PrintSnapshotHistory() {
        snapshot_history.Print([](const Snapshot& snap, uint32_t run_id, uint32_t run_seq, uint64_t global_seq) {
            return to_string(snap.type) + " [run_id=" + std::to_string(run_id) +
                   ", run_seq=" + std::to_string(run_seq) + ", global_seq=" + std::to_string(global_seq) + "]";
        });
    }

}