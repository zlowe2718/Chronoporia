#include "thread_events.h"
#include "thread_manager.h"

namespace chronoporia {

    void ThreadCreateEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        HANDLE thread;
        ReadProcessMemory(globals::process_handle, thread_handle, &thread, sizeof(HANDLE), nullptr);

        // Since the handle is in the child process's handle table we can't use it in this process so we have to dupe it
        HANDLE temp_handle;
        DuplicateHandle(globals::process_handle, thread, GetCurrentProcess(), &temp_handle, 0, false, DUPLICATE_SAME_ACCESS);

        TrackThread(GetThreadId(temp_handle), global_seq);
        CloseHandle(temp_handle);
    }

    void ThreadDestroyEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status_ = thread_ctx.Rax;
        UntrackThread(thread_id_, global_seq);
    }
}