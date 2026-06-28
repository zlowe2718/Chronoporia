#include "thread_events.h"
#include "globals.h"
#include "quill/LogMacros.h"
#include "thread_manager.h"
#include <map>
#include <print>

namespace {
    std::map<DWORD, DWORD> orignal_thread_to_new_thread;
}

namespace chronoporia {

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
        LOG_DEBUG(globals::logger,"Thread Create Event Replay called");
    }

    void ThreadCreateEvent::ReplayEventEnd() {
        LOG_DEBUG(globals::logger,"Thread Create Event Replay End called");
    }

    void ThreadDestroyEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status_ = thread_ctx.Rax;
        UntrackThread(thread_id_, global_seq, run_id, run_sequence);
    }
}