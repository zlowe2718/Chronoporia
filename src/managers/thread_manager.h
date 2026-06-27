#pragma once
#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>

// TODO: Split out thread helper functions and handle map in another file?

namespace chronoporia {

    struct ThreadInfo {
        DWORD thread_id;
        CONTEXT thread_context;
        bool loaded;
    };

    void TrackThread(const DWORD thread_id, const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq);
    void UntrackThread(const DWORD thread_id, const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq);
    void TrackAllProgramThreads(const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq);

    void SnapshotThreads(const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq);

    void CreateThreadHistoryBranch(const uint32_t target_run_id, const uint32_t target_run_seq, const uint32_t new_run_id);

    void RestoreThreadsAtSequence(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id, const uint32_t to_run_seq);
    void TerminateThreadIds(const std::vector<ThreadInfo>& thread_ids, const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq);

}