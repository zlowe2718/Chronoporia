#include "thread_events.h"
#include "thread_manager.h"
#include <map>
#include <print>

namespace {
    std::map<DWORD, DWORD> orignal_thread_to_new_thread;
}

namespace chronoporia {

    // TODO: Need to track all windows calls that use a thread handle?
    // Issue is that on replay the handle returned will be different so if I make a new thread
    // and the code normally caches the handle then i'd have to replace every instance where the handle is cached
    // Or I intercept every windows api that consumes a handle and inject it with the new value?
    // or is this even an issue?
    void ThreadCreateEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        ReadProcessMemory(globals::process_handle, thread_handle, &returned_thread, sizeof(HANDLE), nullptr);

        // Since the handle is in the child process's handle table we can't use it in this process so we have to dupe it
        HANDLE temp_handle;
        DuplicateHandle(globals::process_handle, returned_thread, GetCurrentProcess(), &temp_handle, 0, false, DUPLICATE_SAME_ACCESS);

        TrackThread(GetThreadId(temp_handle), global_seq, run_id, run_sequence);
        CloseHandle(temp_handle);
    }


    // TODO: need to track thread here so we can correctly pull the current thread state from the execution tree
    void ThreadCreateEvent::ReplayEvent() {
        std::print("\nThread Create Event Replay called\n");
    }

    void ThreadCreateEvent::ReplayEventEnd() {
        std::print("\nThread Create Event Replay End called\n");
    }

    void ThreadDestroyEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status_ = thread_ctx.Rax;
        UntrackThread(thread_id_, global_seq, run_id, run_sequence);
    }
}