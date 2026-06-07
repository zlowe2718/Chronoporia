#include "snapshot_log.h"
#include "memory_manager.h"
#include "thread_manager.h"
#include "globals.h"
#include <cstdint>
#include <vector>

namespace {
    uint64_t snapshot_seq = 0;

    std::vector<uint64_t> snapshot_global_seq;
}


namespace chronoporia {

    void SnapshotProcess() {
        // incrememnt the global sequence for deconflicting thread events such as create and then an immediate snapshot
        globals::global_sequence += 1;
        snapshot_seq += 1;

        SnapshotThreads(globals::global_sequence, snapshot_seq);
        SnapshotMemory(globals::global_sequence);

        snapshot_global_seq.push_back(globals::global_sequence);
    }

    uint64_t NearestSnapshotGlobalSeq() {
        return snapshot_global_seq.back();
    }

}