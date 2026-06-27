#pragma once
#include <cstdint>

namespace chronoporia {

    enum class SnapshotType {CoarseSnapshot, MicroSnapshot, LineSnapshot};

    struct Snapshot {
        SnapshotType type;
        uint32_t run_id;
        uint32_t run_sequence;
        uint64_t global_sequence;
    };

    void StartSnapshotHistory();
    void SnapshotProcess(SnapshotType snapshot_type);
    void PrintSnapshotHistory();
}