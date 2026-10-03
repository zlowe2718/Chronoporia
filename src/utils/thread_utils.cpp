#include "thread_utils.h"
#include "globals.h"
#include "breakpoint_manager.h"
#include "nt_undocumented.h"
#include "trampoline.h"
#include "quill/LogMacros.h"
#include <cstdint>
#include <errhandlingapi.h>
#include <map>
#include <future>
#include <memoryapi.h>
#include <winternl.h>

namespace {
    using namespace chronoporia;

    std::map<DWORD, ThreadMeta> thread_id_to_meta;
    std::unordered_map<TlsKind, DWORD> tls_indexes_;
}

namespace chronoporia {

    HANDLE GetThreadHandle(const DWORD thread_id) {
        auto& thread = thread_id_to_meta[thread_id];
        if (thread.handle == nullptr) {
            thread.handle = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_TERMINATE, false, thread_id);
            thread.teb_address = get_teb_address(thread.handle);
            thread_id_to_meta[thread_id] = thread;
        };
        return thread.handle;
    }

    void CloseHandleForThread(const DWORD thread_id, const HANDLE thread_handle) {
        CloseHandle(thread_handle);
        thread_id_to_meta.erase(thread_id);
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

    void SetTrapFlag(const DWORD thread_id) {
        HANDLE thread = GetThreadHandle(thread_id);

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(thread, &ctx);

        ctx.EFlags |= 0x100;
        SetThreadContext(thread, &ctx);
    }

    void ReplaceThreadContext(const DWORD thread_id, const CONTEXT& ctx) {
        HANDLE thread_handle = GetThreadHandle(thread_id);
        if (!SetThreadContext(thread_handle, &ctx)) {
            LOG_WARNING(globals::logger, "Thread error: {}\n"
                   "    Thread handle: {:p}\n"
                   "    Thread id: {}", GetLastError(), thread_handle, thread_id);
        }
    }

    void SuspendAllThreads() {
        for (const auto& [thread_id, thread_meta]: thread_id_to_meta) {
            if(SuspendThread(thread_meta.handle) == static_cast<DWORD>(-1)) {
                LOG_WARNING(globals::logger, "Suspending thread {} failed with error {}", thread_id, GetLastError());
            }
        }        
    }

    void SuspendAllThreadsExceptCurrent(const DWORD current_thread_id) {
        for (const auto& [thread_id, thread_meta]: thread_id_to_meta) {
            if (thread_id != current_thread_id) {
                if(SuspendThread(thread_meta.handle) == static_cast<DWORD>(-1)) {
                    LOG_WARNING(globals::logger,"Suspending thread {} failed with error {}", thread_id, GetLastError());
                }
            }
        }
    }

    void SuspendThreadId(const DWORD thread_id) {
        SuspendThread(GetThreadHandle(thread_id));
    }

    void ResumeAllThreads() {
        for (const auto& [thread_id, thread_meta]: thread_id_to_meta) {
            if(ResumeThread(thread_meta.handle) == static_cast<DWORD>(-1)) {
                LOG_WARNING(globals::logger,"Resuming thread {} failed with error {}", thread_id, GetLastError());
            }
        }        
    }

    void ResumeThreadId(DWORD thread_id) {
        if(ResumeThread(thread_id_to_meta[thread_id].handle) == static_cast<DWORD>(-1)) {
            LOG_WARNING(globals::logger,"Resuming thread {} failed with error {}", thread_id, GetLastError());
        }
    }

    DWORD tls_alloc(TlsKind tls_kind) {
        auto tls_alloc_addr = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TlsAlloc")
        );

        DWORD tls_index;
        if(RunRemoteThreadPumped(globals::process_handle, tls_alloc_addr, nullptr, tls_index)) {
            // tls_index is the child's TLS slot; TLS_OUT_OF_INDEXES (0xFFFFFFFF) on failure.
            LOG_INFO(globals::logger, "child TlsAlloc -> {}", tls_index);            
        }

        tls_indexes_[tls_kind] = tls_index;
        return tls_index;
    }

    bool RunRemoteThreadPumped(HANDLE process, LPTHREAD_START_ROUTINE routine,
                               LPVOID param, DWORD& out_exit_code) {
        using namespace std::chrono_literals;

        // set once the worker knows whether CreateRemoteThread itself succeeded, so
        // we can tell a genuine exit code of 0 apart from a failed injection.
        std::promise<bool> created;
        std::future<bool> created_result = created.get_future();

        // Blocking CreateRemoteThread + join lives on a worker thread; this thread
        // must keep pumping debug events so the injected thread can run to exit.
        std::future<DWORD> worker = std::async(std::launch::async,
            [process, routine, param, &created]() -> DWORD {
                HANDLE thread = CreateRemoteThread(process, nullptr, 0, routine, param, 0, nullptr);
                ResumeThread(thread);
                created.set_value(thread != nullptr);
                if (!thread) {
                    return 0;
                }

                WaitForSingleObject(thread, INFINITE);
                DWORD exit_code = 0;
                GetExitCodeThread(thread, &exit_code);
                CloseHandle(thread);
                return exit_code;
            });

        // Spin-pump until the worker finishes (matches RunThreadRestore's wait_for(0)).
        DEBUG_EVENT de;
        while (worker.wait_for(0ms) != std::future_status::ready) {
            if (!WaitForDebugEvent(&de, 0)) {
                // Nothing queued yet; keep spinning until the injected thread exits.
                continue;
            }

            DWORD continue_status = DBG_CONTINUE;

            // CREATE_THREAD_DEBUG_EVENT / EXIT_THREAD_DEBUG_EVENT for the injected
            // thread need no bookkeeping here (it is ephemeral and deliberately not
            // tracked into the run's thread set) — they just need to be continued.
            if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                const EXCEPTION_RECORD& er = de.u.Exception.ExceptionRecord;
                if (er.ExceptionCode == STATUS_BREAKPOINT) {
                    DWORD thread_id = de.dwThreadId;
                    uintptr_t rip_address = GetRipAddress(thread_id) - 1;
                    if (GetBreakpointType(rip_address, thread_id) == BreakpointType::Permanent) {
                        RedirectToTrampoline(rip_address, thread_id);
                    }
                }
            }

            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continue_status);
        }

        if (!created_result.get()) {
            LOG_ERROR(globals::logger, "CreateRemoteThread failed with error {}", GetLastError());
            return false;
        }

        out_exit_code = worker.get();
        return true;
    }

    uintptr_t get_teb_address(const HANDLE thread_handle) {
        THREAD_BASIC_INFORMATION tbi;
        ULONG ret_length;
        if(!NT_SUCCESS(NtQueryInformationThread(thread_handle, static_cast<::_THREADINFOCLASS>(ThreadBasicInformation), &tbi, sizeof(tbi), &ret_length))) {
            LOG_WARNING(globals::logger, "Could not get tbi from thread with error {}", GetLastError());
            return 0;
        }
        return reinterpret_cast<uintptr_t>(tbi.TebBaseAddress);
    }

    uint64_t read_thread_tls(const DWORD thread_id, const TlsKind tls_kind) {
        uint32_t tls_offset;
        uint32_t tls_index = tls_indexes_[tls_kind];
        if (tls_index < 64) {
            tls_offset = static_cast<uint32_t>(offsetof(TEB, TlsSlots) + 8 * tls_index);
        } else {
            // TODO: not handled yet.  TlsExpansionSlots are pointers so we need to dereference those first
            LOG_ERROR(globals::logger, "Tls index >= 64 not currently handled");
            return 0;
        }
        uintptr_t tls_address = thread_id_to_meta[thread_id].teb_address + tls_offset;
        uint64_t value;
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(tls_address), &value, 8, nullptr);
        return value;
    }

    uint64_t read_thread_tls(const DWORD thread_id, const uint32_t tls_index) {
        uint32_t tls_offset;
        if (tls_index < 64) {
            tls_offset = static_cast<uint32_t>(offsetof(TEB, TlsSlots) + 8 * tls_index);
        } else {
            // TODO: not handled yet.  TlsExpansionSlots are pointers so we need to dereference those first
            LOG_ERROR(globals::logger, "Tls index >= 64 not currently handled");
            return 0;
        }
        uintptr_t tls_address = thread_id_to_meta[thread_id].teb_address + tls_offset;
        uint64_t value;
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(tls_address), &value, 8, nullptr);
        return value;        
    }

    void update_thread_tls(const DWORD thread_id, const TlsKind tls_kind, const uint64_t value) {
        uint32_t tls_offset;
        uint32_t tls_index = tls_indexes_[tls_kind];
        if (tls_index < 64) {
            tls_offset = static_cast<uint32_t>(offsetof(TEB, TlsSlots) + 8 * tls_index);
        } else {
            // TODO: not handled yet.  TlsExpansionSlots are pointers so we need to dereference those first
            LOG_ERROR(globals::logger, "Tls index >= 64 not currently handled");
            return;
        }
        uintptr_t tls_address = thread_id_to_meta[thread_id].teb_address + tls_offset;

        // Page protections of the TEB should be PAGE_READWRITE so this should be safe
        WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(tls_address), &value, 8, nullptr);        
    }

    void update_all_thread_tls(const TlsKind tls_kind, const uint64_t value) {
        for (const auto& [thread_id, _] : thread_id_to_meta) {
            update_thread_tls(thread_id, tls_kind, value);
        }
    }
}