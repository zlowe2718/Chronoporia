#include "memory_utils.h"
#include "globals.h"
#include "quill/LogMacros.h"
#include <span>

namespace chronoporia {
    void virtual_free(void *allocation_base) {
        if (!VirtualFreeEx(globals::process_handle, allocation_base, 0, MEM_RELEASE))
        {
            LOG_DEBUG(globals::logger, "VirtualFreeEx failed:\n"
                "    error:    {}\n"
                "    address:  {:p}", GetLastError(), allocation_base);
        }
    }

    void virtual_reserve_memory(void *address, const uint64_t total_region_size, const DWORD alloc_protect) {
        if (!VirtualAllocEx(
            globals::process_handle,
            address,
            total_region_size,
            MEM_RESERVE,
            alloc_protect
        ))
        {
            LOG_DEBUG(globals::logger,"VirtualAllocEx (MEM_RESERVE) failed:\n"
                "    error:    %ld\n"
                "    address:  %p\n"
                "    size:     %lld\n", GetLastError(), address, total_region_size);
        }    
    }

    void virtual_write(void *address, const std::span<const std::byte> memory) {
        if (!WriteProcessMemory(globals::process_handle, address, memory.data(), memory.size(), nullptr))
        {
            LOG_DEBUG(globals::logger,"WriteProcessMemory failed:\n"
                "    error:    {}\n"
                "    address:  {:p}\n"
                "    size:     {}", GetLastError(), address, memory.size());
        }
    }

    void virtual_alloc_full_permissions(void *address, const uint64_t region_size) {
        if (!VirtualAllocEx(globals::process_handle, address, region_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
        {
            LOG_DEBUG(globals::logger,"VirtualAllocEx (MEM_COMMIT) failed:\n"
                "    error:    {}\n"
                "    address:  {:p}\n"
                "    size:     {}", GetLastError(), address, region_size);
        }
    }

    DWORD virtual_protect(void *address, const uint64_t region_size, const DWORD new_protect) {
        DWORD old_protect;
        if (!VirtualProtectEx(globals::process_handle, address, region_size, new_protect, &old_protect))
        {
            LOG_DEBUG(globals::logger,"VirtualProtectEx (TEB) failed:\n"
                "    error:    {}\n"
                "    address:  {:p}\n"
                "    size:     {}", GetLastError(), address, region_size);
        }
        return old_protect;
    }
}