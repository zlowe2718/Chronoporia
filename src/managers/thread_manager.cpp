#include "thread_manager.h"
#include "thread_utils.h"
#include "globals.h"
#include "partially_persistent_arrays.h"
#include <TlHelp32.h>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
    // Thread id to thread creation/snapshot/deletion history
    std::unordered_map<
        DWORD, 
        chronoporia::PartiallyPersistentArray<chronoporia::ThreadInfo>
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

    void TrackThread(const DWORD thread_id, const uint64_t global_seq) {
        HANDLE thread = GetThreadHandle(thread_id);
        CONTEXT ctx = GetThreadContext(thread);
        ThreadInfo thread_info {0, thread_id, ctx, true};

        current_thread_ids.insert(thread_id);

        if (process_thread_history.contains(thread_id)) {
            process_thread_history[thread_id].update(global_seq, std::move(thread_info));
        } else {
            process_thread_history[thread_id] = PartiallyPersistentArray<ThreadInfo> {global_seq, std::move(thread_info)};
        }
    }

    void UntrackThread(const DWORD thread_id, const uint64_t global_seq) {
        current_thread_ids.erase(thread_id);

        if (process_thread_history.contains(thread_id)) {
            HANDLE thread = GetThreadHandle(thread_id);
            CloseHandleForThread(thread_id, thread);

            ThreadInfo thread_info {0, thread_id, CONTEXT{}, false};
            process_thread_history[thread_id].update(global_seq, std::move(thread_info));
        } else {
            printf("Untracking thread %lu before tracking", thread_id);
        }
    }

    void TrackAllProgramThreads(const uint64_t global_seq) {
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
                        TrackThread(te.th32ThreadID, global_seq);
                    }
                } while (Thread32Next(h, &te));
            }
            CloseHandle(h);
        }              
    }

    void RestoreThreadsAtSequence(const uint64_t target_seq) {
        std::vector<ThreadInfo> snapped_threads {};
        std::vector<ThreadInfo> current_threads {};

        ThreadInfo fetched_thread_info {};

        for (const auto& [_, partially_persistent_thread] : process_thread_history) {
            if (target_seq >= partially_persistent_thread.created_version) {
                fetched_thread_info = partially_persistent_thread.get(target_seq);
                if (fetched_thread_info.loaded) {
                    snapped_threads.push_back(fetched_thread_info);
                }
            }
            
            fetched_thread_info = partially_persistent_thread.get(globals::global_sequence);
            if (fetched_thread_info.loaded) {
                current_threads.push_back(fetched_thread_info);
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
            TerminateThreadIds(current_threads);
        }
    }

    void TerminateThreadIds(const std::vector<ThreadInfo>& thread_infos) {
        for (const ThreadInfo& thread_info: thread_infos) {
            // TODO: Do I want ExitThread instead?
            DWORD thread_id = thread_info.thread_id;
            HANDLE thread = GetThreadHandle(thread_id);

            TerminateThread(thread, 0);
            CloseHandleForThread(thread_id, thread);
        }
    }

    void SnapshotThreads(const uint64_t global_seq, const uint64_t snapshot_seq) {
        for (const DWORD thread_id : current_thread_ids) {
            HANDLE thread = GetThreadHandle(thread_id);
            CONTEXT ctx = GetThreadContext(thread);
            ThreadInfo thread_info {snapshot_seq, thread_id, ctx, true};
            
            process_thread_history[thread_id].update(global_seq, std::move(thread_info));
        }
    }

}

