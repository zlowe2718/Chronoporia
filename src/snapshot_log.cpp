#include "snapshot_log.h"
#include "memory_manager.h"
#include "thread_manager.h"
#include "execution_tree.h"
#include "globals.h"

namespace {
    chronoporia::ExecutionTree<chronoporia::Snapshot> snapshot_history {{}, 0, 0, 0};
}


namespace chronoporia {

    // TODO: is there a better way to do this because I don't want an empty ExecutionTree?
    void StartSnapshotHistory() {
        Snapshot snap { SnapshotType::CoarseSnapshot, globals::run_id, globals::run_sequence, globals::global_sequence };
        snapshot_history = ExecutionTree<Snapshot> {snap, globals::run_id, globals::run_sequence, globals::global_sequence};
    }

    void SnapshotProcess(SnapshotType snapshot_type) {
        Snapshot new_snapshot {snapshot_type, globals::run_id, globals::run_sequence, globals::global_sequence};

        SnapshotThreads(globals::global_sequence, globals::run_id, globals::run_sequence);
        SnapshotMemory(globals::global_sequence, globals::run_id, globals::run_sequence);

        snapshot_history.AddChild(std::move(new_snapshot), globals::run_id, globals::run_sequence, globals::global_sequence);

        globals::run_sequence += 1;
    }

}