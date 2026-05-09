#pragma once
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
    void TrackThread(const DWORD thread_id, const uint64_t global_seq);
    void UntrackThread(const DWORD thread_id, const uint64_t global_seq);
    void TrackAllProgramThreads(const uint64_t global_seq);

    void RestoreThreadsAtSequence(const uint64_t global_seq);
    void TerminateThreadIds(const std::vector<ThreadInfo>& thread_ids);

}