#pragma once
#include <cstdint>
#include <map>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>

namespace chronoporia {

    enum class TlsKind { BranchCounter, BranchTarget };

    struct ThreadMeta {
        HANDLE handle;
        uintptr_t teb_address;
    };

    HANDLE GetThreadHandle(const DWORD thread_id);
    void CloseHandleForThread(const DWORD thread_id, const HANDLE thread_handle);

    uint64_t GetRipAddress(const DWORD thread_id);
    CONTEXT RollBackInstructionPointRegister(const DWORD thread_id);
    CONTEXT SetRipAddress(const DWORD thread_id, const uintptr_t new_rip_address);
    CONTEXT GetThreadContextFromId(const DWORD thread_id);
    void ReplaceThreadContext(const DWORD thread_id, const CONTEXT& ctx);
    void SetTrapFlag(const DWORD thread_id);

    void SuspendAllThreads();
    void SuspendAllThreadsExceptCurrent(const DWORD thread_id);
    void SuspendThreadId(const DWORD thread_id);
    // We can safely resume all threads since if the suspend count is 0 windows doesn't care or throw an error
    void ResumeAllThreads();
    void ResumeThreadId(const DWORD thread_id);

    // Runs a no-argument, kernel32-style routine (e.g. TlsAlloc) as a remote thread
    // in the debuggee and returns its 32-bit exit code (for TlsAlloc, the allocated
    // TLS index) via out_exit_code.
    //
    // The CreateRemoteThread + join runs on a worker thread so this (the debugger
    // main thread) stays free to pump WaitForDebugEvent. Joining the remote thread
    // on the main thread would deadlock: the injected thread can't run to exit until
    // we ContinueDebugEvent its CREATE_THREAD/EXIT_THREAD events, which we can't do
    // while blocked in WaitForSingleObject. See RunThreadRestore() for the same idiom.
    //
    // Returns false if CreateRemoteThread failed (out_exit_code untouched).
    bool RunRemoteThreadPumped(HANDLE process, LPTHREAD_START_ROUTINE routine,
                               LPVOID param, DWORD& out_exit_code);

    // Get the Thread Environment Block (TEB) address for the thread
    uintptr_t get_teb_address(const HANDLE thread_handle);

    // TLS functions
    // after allocating a tls slot, this guarantees the same index across all threads
    // (including threads when they're created)

    // Allocate a new Thread Local Storage (TLS) slot for the kind of data to store
    // Returns the index of the TLS slot
    DWORD tls_alloc(const TlsKind tls_kind);

    // Read the thread's TLS slot for the data kind
    uint64_t read_thread_tls(const DWORD thread_id, const TlsKind tls_kind);
    uint64_t read_thread_tls(const DWORD thread_id, const uint32_t tls_index);

    void update_thread_tls(const DWORD thread_id, const TlsKind tls_kind, const uint64_t value);
    void update_all_thread_tls(const TlsKind tls_kind, const uint64_t value);
}