#pragma once
#include <cstdint>
#include <optional>

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
    bool ValidSnapshotIdSeq(uint32_t run_id, uint32_t run_seq);
    std::optional<uint64_t> GetGlobalSequence(uint32_t run_id, uint32_t run_seq);
    std::optional<Snapshot> get_nearest_snapshot();
    void CreateSnapshotHistoryBranch(uint32_t from_run_id, uint32_t from_run_seq, uint32_t to_run_id);
}