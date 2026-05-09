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

    // TODO: Need to rethink how to restore dlls since I'll have to tell the thread to run my shell code
    //  Then listen for the breakpoint that says dlls were unloaded and then reset the threads and memory
    //  This will probably need to be split into two functions
    void CoarseSnapshotEvent::apply() {
        RestoreDLLsAtSequence(global_seq);
        RestoreThreadsAtSequence(global_seq);
        RestoreMemoryAtSequence(global_seq);
    }
}