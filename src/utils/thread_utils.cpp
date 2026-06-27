#include "thread_utils.h"
#include <map>

namespace {
    std::map<DWORD, HANDLE> thread_id_to_handle;
}

namespace chronoporia {

    HANDLE GetThreadHandle(const DWORD thread_id) {
        HANDLE thread = thread_id_to_handle[thread_id];
        if (thread == nullptr) {
            thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_TERMINATE, false, thread_id);
            thread_id_to_handle[thread_id] = thread;
        };
        return thread;
    }

    void CloseHandleForThread(const DWORD thread_id, const HANDLE thread_handle) {
        CloseHandle(thread_handle);
        thread_id_to_handle.erase(thread_id);
    }

    uint64_t GetRipAddress(const DWORD thread_id) {
        HANDLE thread = GetThreadHandle(thread_id);

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_CONTROL;
        GetThreadContext(thread, &ctx);        

        return ctx.Rip;
    }

    CONTEXT RollBackInstructionPointRegister(const DWORD thread_id) {
        HANDLE thread = GetThreadHandle(thread_id);

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(thread, &ctx);

        ctx.Rip -= 1;
        SetThreadContext(thread, &ctx);

        return ctx;
    }

    CONTEXT SetRipAddress(const DWORD thread_id, const uintptr_t new_rip_address) {
        HANDLE thread = GetThreadHandle(thread_id);

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(thread, &ctx);

        ctx.Rip = new_rip_address;
        SetThreadContext(thread, &ctx);

        return ctx;        
    }

    CONTEXT GetThreadContextFromId(const DWORD thread_id) {
        HANDLE thread = GetThreadHandle(thread_id);

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(thread, &ctx);

        return ctx;           
    }

    void ReplaceThreadContext(const DWORD thread_id, const CONTEXT& ctx) {
        HANDLE thread_handle = GetThreadHandle(thread_id);
        if (!SetThreadContext(thread_handle, &ctx)) {
            printf("Thread error: %ld\n"
                   "    Thread handle: %p\n"
                   "    Thread id: %ld", GetLastError(), thread_handle, thread_id);
        }
    }

    void SuspendAllThreads() {
        for (const auto [thread_id, thread_handle]: thread_id_to_handle) {
            if(SuspendThread(thread_handle) == static_cast<DWORD>(-1)) {
                printf("Suspending thread %ld failed with error %ld\n", thread_id, GetLastError());
            }
        }        
    }

    void SuspendAllThreadsExceptCurrent(const DWORD current_thread_id) {
        for (const auto [thread_id, thread_handle]: thread_id_to_handle) {
            if (thread_id != current_thread_id) {
                if(SuspendThread(thread_handle) == static_cast<DWORD>(-1)) {
                    printf("Suspending thread %ld failed with error %ld\n", thread_id, GetLastError());
                }
            }
        }
    }

    void SuspendThreadId(const DWORD thread_id) {
        SuspendThread(GetThreadHandle(thread_id));
    }

    void ResumeAllThreads() {
        for (const auto [thread_id, thread_handle]: thread_id_to_handle) {
            if(ResumeThread(thread_handle) == static_cast<DWORD>(-1)) {
                printf("Resuming thread %ld failed with error %ld\n", thread_id, GetLastError());
            }
        }        
    }

    void ResumeThreadId(DWORD thread_id) {
        if(ResumeThread(thread_id_to_handle[thread_id]) == static_cast<DWORD>(-1)) {
            printf("Resuming thread %ld failed with error %ld\n", thread_id, GetLastError());
        }
    }

}