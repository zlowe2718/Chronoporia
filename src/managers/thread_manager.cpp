#include "thread_manager.h"
#include "quill/LogMacros.h"
#include "thread_utils.h"
#include "execution_tree.h"
#include "globals.h"
#include <TlHelp32.h>
#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
    // Thread id to thread creation/snapshot/deletion history
    std::unordered_map<
        DWORD, 
        chronoporia::ExecutionTree<chronoporia::ThreadInfo>
    > process_thread_history;

    std::set<DWORD> current_thread_ids;

    CONTEXT GetThreadContext(const HANDLE thread_handle) {
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(thread_handle, &ctx);
        return ctx;
    }
}

namespace chronoporia {

    void TrackThread(const DWORD thread_id, const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        HANDLE thread = GetThreadHandle(thread_id);
        CONTEXT ctx = GetThreadContext(thread);
        ThreadInfo thread_info {thread_id, ctx, true};

        current_thread_ids.insert(thread_id);

        if (process_thread_history.contains(thread_id)) {
            process_thread_history.at(thread_id).AddChild(std::move(thread_info), run_id, run_seq, global_seq);
        } else {
            process_thread_history.try_emplace(thread_id, std::move(thread_info), run_id, run_seq, global_seq);
        }
    }

    void UntrackThread(const DWORD thread_id, const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        current_thread_ids.erase(thread_id);

        if (process_thread_history.contains(thread_id)) {
            HANDLE thread = GetThreadHandle(thread_id);
            CloseHandleForThread(thread_id, thread);

            ThreadInfo thread_info {thread_id, CONTEXT{}, false};
            process_thread_history.at(thread_id).AddChild(std::move(thread_info), run_id, run_seq, global_seq);
        } else {
            LOG_WARNING(globals::logger, "Untracking thread {} before tracking", thread_id);
        }
    }

    void TrackAllProgramThreads(const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, globals::process_id);
        if (h != INVALID_HANDLE_VALUE)
        {
            THREADENTRY32 te;
            te.dwSize = sizeof(THREADENTRY32);
            if (Thread32First(h, &te))
            {
                do
                {
                    if (te.th32OwnerProcessID == globals::process_id)
                    {
                        TrackThread(te.th32ThreadID, global_seq, run_id, run_seq);
                    }
                } while (Thread32Next(h, &te));
            }
            CloseHandle(h);
        }              
    }

    void RestoreThreadsAtSequence(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id, const uint32_t to_run_seq) {
        std::vector<ThreadInfo> snapped_threads {};
        std::vector<ThreadInfo> current_threads {};

        std::optional<ThreadInfo> from_thread_info {};
        std::optional<ThreadInfo> to_thread_info {};
        
        for (const auto& [_, execution_tree] : process_thread_history) {
            to_thread_info = execution_tree.GetState(to_run_id, to_run_seq);
            if (to_thread_info && to_thread_info->loaded) {
                snapped_threads.push_back(*to_thread_info);
            }
            
            from_thread_info = execution_tree.GetState(from_run_id, from_run_seq);
            if (from_thread_info && from_thread_info->loaded) {
                current_threads.push_back(*from_thread_info);
            }
        }        

        // Restore each thread context
        for (const ThreadInfo& snapped_thread_info : snapped_threads) {
            const auto current_thread_id_itr = std::find_if(current_threads.begin(), current_threads.end(),
                [&snapped_thread_info](const ThreadInfo& current_thread) {return snapped_thread_info.thread_id == current_thread.thread_id;}
            );
            if (current_thread_id_itr != current_threads.end()) {
                ReplaceThreadContext(snapped_thread_info.thread_id, snapped_thread_info.thread_context);
                // delete it from the list so we know whats left over
                current_threads.erase(current_thread_id_itr);
            } else {
                // TODO: create remote thread here
            }
        }
        // Destroy any leftover threads
        if (current_threads.size() > 0) {
            TerminateThreadIds(current_threads, globals::global_sequence, from_run_id, from_run_seq);
        }
    }

    void TerminateThreadIds(const std::vector<ThreadInfo>& thread_infos, const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        for (const ThreadInfo& thread_info: thread_infos) {
            DWORD thread_id = thread_info.thread_id;
            HANDLE thread = GetThreadHandle(thread_id);

            TerminateThread(thread, 0);
            // Mark the thread unloaded immediately - without this, process_thread_history keeps
            // reporting it as "current"/loaded forever, so a later restore matches this dead
            // thread_id against the snapshot and tries to SetThreadContext on a handle that no
            // longer resolves to a live thread (ERROR_INVALID_HANDLE).
            UntrackThread(thread_id, global_seq, run_id, run_seq);
        }
    }

    void SnapshotThreads(const uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        for (const DWORD thread_id : current_thread_ids) {
            HANDLE thread = GetThreadHandle(thread_id);
            std::optional<CONTEXT> ctx = GetThreadContext(thread);
            if (!ctx) {
                LOG_WARNING(globals::logger, "SnapshotThreads: GetThreadContext failed, not recording a snapshot for this thread:\n"
                    "    error:     {}\n"
                    "    thread_id: {}", GetLastError(), thread_id);
                continue;
            }
            ThreadInfo thread_info {thread_id, *ctx, true};

            process_thread_history.at(thread_id).AddChild(std::move(thread_info), run_id, run_seq, global_seq);
        }
    }


    void CreateThreadHistoryBranch(const uint32_t target_run_id, const uint32_t target_run_seq, const uint32_t new_run_id) {
        for (auto& [_, execution_tree] : process_thread_history) {
            execution_tree.RevertToState(target_run_id, target_run_seq, new_run_id);
        }
    }
}

