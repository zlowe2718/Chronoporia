#include "snapshot_events.h"
#include "memory_manager.h"
#include "module_manager.h"
#include "thread_manager.h"

namespace chronoporia {

    std::unique_ptr<CoarseSnapshotEvent> CreateCoarseSnapshotEvent(const DWORD thread_id, bool only_dirty_pages) {
        auto snapshot = std::make_unique<CoarseSnapshotEvent>(thread_id);
        TrackAllProgramThreads(snapshot->global_seq);
        if (!only_dirty_pages) {
            CoarseSnapshot(snapshot->global_seq);
        }
        return snapshot;
    }
}