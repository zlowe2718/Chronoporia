#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>

namespace chronoporia {

    HANDLE GetThreadHandle(const DWORD thread_id);
    void CloseHandleForThread(const DWORD thread_id, const HANDLE thread_handle);

    uint64_t GetRipAddress(const DWORD thread_id);
    CONTEXT RollBackInstructionPointRegister(const DWORD thread_id);
    CONTEXT SetRipAddress(const DWORD thread_id, const uintptr_t new_rip_address);
    CONTEXT GetThreadContextFromId(const DWORD thread_id);
    void ReplaceThreadContext(const DWORD thread_id, const CONTEXT& ctx);

    void SuspendAllThreads();
    void SuspendAllThreadsExceptCurrent(const DWORD thread_id);
    void SuspendThreadId(const DWORD thread_id);
    // We can safely resume all threads since if the suspend count is 0 windows doesn't care or throw an error
    void ResumeAllThreads();
    void ResumeThreadId(const DWORD thread_id);


}