#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>
#include <string>
#include "base_event.h"

namespace chronoporia {

    
    class SharedLibraryLoadEvent : public BaseEvent {
    public:
        std::wstring dll_path;
        ULONG dll_characteristics;
        std::wstring dll_name;
        HMODULE *dll_handle_out;
        NTSTATUS return_status; 

        HMODULE dll_handle;
        // LdrLoadLibrary
        //     IN PCWSTR dll_path
        //     IN PULONG dll_characteristics
        //     IN PCUNICODE_STRING dll_name
        //     OUT PVOID *dll_handle;
        // TODO: readprocessmemory may not be needed if I track memory addresses deterministically
        SharedLibraryLoadEvent(DWORD thread_id, uintptr_t event_rip, const CONTEXT& thread_ctx)
            : BaseEvent(thread_id, event_rip)
            {
                dll_path.resize(260);
                ReadProcessMemory(globals::process_handle, reinterpret_cast<PCWSTR>(thread_ctx.Rcx), dll_path.data(), 260, nullptr);
                ReadProcessMemory(globals::process_handle, reinterpret_cast<PULONG>(thread_ctx.Rdx), &dll_characteristics, sizeof(ULONG), nullptr);
                
                UNICODE_STRING temp;
                ReadProcessMemory(globals::process_handle, reinterpret_cast<PCUNICODE_STRING>(thread_ctx.R8), &temp, sizeof(UNICODE_STRING), nullptr);
                
                dll_name.resize(temp.Length);
                ReadProcessMemory(globals::process_handle, temp.Buffer, dll_name.data(), temp.Length, nullptr);
                
                dll_handle_out = reinterpret_cast<HMODULE *>(thread_ctx.R9);
            }
    
        virtual void FinishEvent(const CONTEXT& thread_ctx) override;
        virtual void ReplayEvent() override;
        virtual void ReplayEventEnd() override;
    };

    class SharedLibraryUnloadEvent : public BaseEvent {
    public:
        HMODULE dll_handle;
        NTSTATUS return_status; 

        // LdrLoadLibrary
        //     IN PVOID dll_handle
        SharedLibraryUnloadEvent(DWORD thread_id, uintptr_t event_rip, const CONTEXT& thread_ctx)
            : BaseEvent(thread_id, event_rip)
            {
                dll_handle = reinterpret_cast<HMODULE>(thread_ctx.Rcx);
            }
    
        virtual void FinishEvent(const CONTEXT& thread_ctx) override;
        virtual void ReplayEvent() override;
        virtual void ReplayEventEnd() override;
    };
}