#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "nt_undocumented.h"
#include "base_event.h"

// TODO: May need to handle NtThreadCreate later for earlier windows versions

namespace chronoporia {
    struct ThreadCreateStackArgs {
        IN PUSER_THREAD_START_ROUTINE start_routine;
        IN OPTIONAL PVOID argument;
        IN ULONG create_flags;
        IN SIZE_T zero_bits;
        IN SIZE_T stack_size;
        IN SIZE_T maximum_stack_size;
        IN OPTIONAL PPS_ATTRIBUTE_LIST attribute_list;
    };

    // For NtCreateThreadEx
    class ThreadCreateEvent : public BaseEvent {
    public:
        OUT PHANDLE thread_handle;
        IN ACCESS_MASK desired_access;
        IN OPTIONAL PCOBJECT_ATTRIBUTES object_attributes;
        IN HANDLE process_handle;
        ThreadCreateStackArgs stack_args;
        
        NTSTATUS return_status;
        HANDLE returned_thread;

        // TODO: read process memory of the in pointers like object_attributes, start_routine, argument...
        ThreadCreateEvent(DWORD thread_id, uintptr_t event_rip, const CONTEXT& thread_ctx)
            : BaseEvent(thread_id, event_rip)
            {
                thread_handle = reinterpret_cast<PHANDLE>(thread_ctx.Rcx);
                desired_access = thread_ctx.Rdx;
                object_attributes = reinterpret_cast<PCOBJECT_ATTRIBUTES>(thread_ctx.R8);
                process_handle = reinterpret_cast<HANDLE>(thread_ctx.R9);
                ReadStackArgs(thread_ctx.Rsp, stack_args);
            }

        virtual void FinishEvent(const CONTEXT& thread_ctx) override;
    };

    // NtTerminateThread
    class ThreadDestroyEvent : public BaseEvent {
    public:
        IN OPTIONAL HANDLE thread_handle_;
        IN NTSTATUS exit_status_;
        NTSTATUS return_status_;

        DWORD thread_id_;

        ThreadDestroyEvent(DWORD thread_id, uintptr_t event_rip, const CONTEXT& thread_ctx) 
            : BaseEvent(thread_id, event_rip)
            {
                thread_handle_ = reinterpret_cast<HANDLE>(thread_ctx.Rcx);
                exit_status_ = thread_ctx.Rdx;

                HANDLE temp_handle;
                DuplicateHandle(globals::process_handle, thread_handle_, GetCurrentProcess(), &temp_handle, 0, false, DUPLICATE_SAME_ACCESS);

                thread_id_ = GetThreadId(temp_handle);
                CloseHandle(temp_handle);
            }

        virtual void FinishEvent(const CONTEXT& thread_ctx) override;
    };
}