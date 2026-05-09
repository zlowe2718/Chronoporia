#pragma once
#include "base_event.h"

namespace chronoporia {
    struct NtAllocateVirtualMemoryEvent : public BaseEvent {
        HANDLE process_handle;
        PVOID *base_address;
        ULONG_PTR zero_bits;
        PSIZE_T region_size;
        ULONG allocation_type;
        ULONG protect;
    };
}