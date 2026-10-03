#pragma once
#include <Windows.h>
#include <cstdint>
#include <span>

namespace chronoporia {

    // Wrapper around VirtualFreeEx with error checking and logging
    void virtual_free(void *allocation_base);

    // Wrapper around VirtualAllocEx to reserve process memory with error checking and logging
    void virtual_reserve_memory(void *address, const uint64_t total_region_size, const DWORD alloc_protect);

    // Wrapper around WriteProcessMemory with error checking and logging
    void virtual_write(void *address, const std::span<const std::byte> memory);

    // Wrapper around VirtualAllocEx with PAGE_EXECUTE_READWRITE permissions and error checking and logging
    void virtual_alloc_full_permissions(void *address, const uint64_t region_size);

    // Wrapper around VirtualProtectEx with error checking and logging
    // Returns the original protection value
    DWORD virtual_protect(void *address, const uint64_t region_size, const DWORD new_protect);
}