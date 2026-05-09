#include "memory_manager.h"
#include "globals.h"
#include <basetsd.h>
#include <memoryapi.h>

namespace {
    // An unordered map of allocation base ptr to the partially persistent memory region history
    std::unordered_map<uintptr_t, chronoporia::MemoryRegionHistory<4096>> process_memory_history;

    std::vector<chronoporia::PageMemory> GetBlockMemory(const chronoporia::BlockHistory<4096>& block_history, uint64_t block_size, uint64_t target_sequence) {
        std::vector<chronoporia::PageMemory> rebuilt_block_memory;
        rebuilt_block_memory.resize(block_size / 4096);

        for (const auto& [page_address, persistent_cache_array] : block_history.page_history) {
            // Need the offset since we're looping over an unordered_map
            uint64_t page_offset = (page_address - block_history.base_address) / 4096;
            memcpy(
                rebuilt_block_memory.data() + page_offset, 
                persistent_cache_array.get(target_sequence).data(), 
                4096
            );
        }

        return rebuilt_block_memory;
    }

    void inline FreeMemory(void *allocation_base) {
        if (!VirtualFreeEx(globals::process_handle, allocation_base, 0, MEM_RELEASE))
        {
            printf("VirtualFreeEx failed:\n"
                "    error:    %ld\n"
                "    address:  %p\n", GetLastError(), allocation_base);
        }
    }

}

namespace chronoporia {

void RestoreMemoryAtSequence(uint64_t target_sequence) {
    // First Free all private allocations
    MEMORY_BASIC_INFORMATION m;
    for (char *address = NULL; VirtualQueryEx(globals::process_handle, address, &m, sizeof(m)) == sizeof(m);
            address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
    {
        if (m.State == MEM_FREE || m.Type == MEM_IMAGE || m.Type == MEM_MAPPED) continue;
        // Skip KUSER_SHARED_DATA
        if (address >= globals::kUserSharedDataBaseAddress && address <= globals::kUserSharedDataEndAddress) continue;
        // TODO: Is this needed? If we free the allocation base this should never trigger? Only act at the allocation base to avoid double-free within the same region
        if (m.BaseAddress != m.AllocationBase) continue;
        // TODO: no need to free TEB.  Do I need to track this in ThreadInfo
        // if (restore_snapshot.thread_contexts.count(m.BaseAddress)) continue;
        FreeMemory(m.AllocationBase);
    }

    for (const auto& [allocation_base, region_history] : process_memory_history) {
        auto event_idx = std::upper_bound(region_history.events.begin(), region_history.events.end(), target_sequence,
            [](uint64_t seq, const MemoryEvent& event) {
                return seq < event.global_sequence;
            }
        ) - region_history.events.begin();

        // If the most recent event is freed then skip over this
        if (region_history.events[event_idx - 1].event_type == MemoryEventType::FREED) continue;

        VirtualQueryEx(globals::process_handle, reinterpret_cast<void *>(allocation_base), &m, sizeof(m));

        bool overwrite_address = m.State != MEM_FREE;
        
        // If the memory is already freed then we need to reserve the memory before we can write to it
        if (!overwrite_address) {
            if (!VirtualAllocEx(
                globals::process_handle, 
                reinterpret_cast<void *>(allocation_base), 
                region_history.total_region_size, 
                MEM_RESERVE, 
                region_history.allocation_protect
            ))
            {
                printf("VirtualAllocEx (MEM_RESERVE) failed:\n"
                    "    error:    %ld\n"
                    "    address:  %p\n"
                    "    size:     %lld\n", GetLastError(), allocation_base, region_history.total_region_size);
            }
        }

        for (const auto& [base_address, block_history] : region_history.block_history) {
            DWORD old_protect;
            SIZE_T bytes_written;

            auto mbi_idx = std::upper_bound(block_history.mbi_history.begin(), block_history.mbi_history.end(), target_sequence,
                [](uint64_t seq, const MBIHistory& event) {
                    return seq < event.global_sequence;
                }
            ) - block_history.mbi_history.begin();

            const MBIHistory& mbi = block_history.mbi_history[mbi_idx - 1];

            void *base_address_ptr = reinterpret_cast<void *>(block_history.base_address);
            uint64_t block_size = mbi.size;

            std::vector<PageMemory> rebuilt_block = GetBlockMemory(block_history, block_size, target_sequence);

            if (mbi.state == MEM_COMMIT)
            {
                // Only need to commit fresh pages — in overwrite mode the page is already committed
                // (image/mapped pages, or private pages we kept in pass 1). Calling VirtualAllocEx
                // on an already-committed page would fail, so skip it.
                if (!overwrite_address)
                {
                    if (!VirtualAllocEx(globals::process_handle, base_address_ptr, block_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                    {
                        printf("VirtualAllocEx (MEM_COMMIT) failed:\n"
                            "    error:    %ld\n"
                            "    address:  %p\n"
                            "    size:     %lld\n", GetLastError(), base_address_ptr, block_size);
                    }
                } else if (mbi.protect & PAGE_GUARD) {
                    // This is the Thread Stack memory blocks
                    if (!VirtualProtectEx(globals::process_handle, base_address_ptr, block_size, mbi.protect & ~PAGE_GUARD, &old_protect))
                    {
                        printf("VirtualProtectEx (TEB) failed:\n"
                            "    error:    %ld\n"
                            "    address:  %p\n"
                            "    size:     %lld\n", GetLastError(), base_address_ptr, block_size);
                    }                
                } else if (mbi.type == MEM_IMAGE) {
                    // This is the DLLs.  We're remapping to WriteCopy so that we don't mess with the underlying dll memory for other programs sharing it
                    // This can also be mapped files
                    if (!VirtualProtectEx(globals::process_handle, base_address_ptr, block_size, PAGE_EXECUTE_WRITECOPY, &old_protect))
                    {
                        printf("VirtualProtectEx (IMAGE) failed:\n"
                            "    error:    %ld\n"
                            "    address:  %p\n"
                            "    size:     %lld\n", GetLastError(), base_address_ptr, block_size);
                    }                    
                }
                if (!WriteProcessMemory(globals::process_handle, base_address_ptr, rebuilt_block.data(), block_size, &bytes_written))
                {
                    printf("WriteProcessMemory failed:\n"
                        "    error:    %ld\n"
                        "    address:  %p\n"
                        "    size:     %lld\n", GetLastError(), base_address_ptr, block_size);
                }
                if (!VirtualProtectEx(globals::process_handle, base_address_ptr, block_size, mbi.protect, &old_protect))
                {
                    printf("VirtualProtectEx failed:\n"
                        "    error:    %ld\n"
                        "    address:  %p\n"
                        "    size:     %lld\n", GetLastError(), base_address_ptr, block_size);
                }
            }        
        }
    }
};

void CoarseSnapshot(uint64_t global_sequence) {
    MEMORY_BASIC_INFORMATION m {};

    for (void *address = NULL; VirtualQueryEx(globals::process_handle, address, &m, sizeof(m)) == sizeof(m);
        address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
    {
        // No need to track Free blocks
        if (m.State == MEM_FREE) continue;

        void *current_address = m.BaseAddress;

        // Exclude KUSER_SHARED_DATA — OS-managed read-only page, no need to snapshot
        if (current_address >= globals::kUserSharedDataBaseAddress && current_address <= globals::kUserSharedDataEndAddress) continue;
        
        uintptr_t allocation_base = reinterpret_cast<uintptr_t>(m.AllocationBase);
        uintptr_t base_address = reinterpret_cast<uintptr_t>(m.BaseAddress);

        // Start a new memory region at each allocation base boundary
        if (current_address == m.AllocationBase && !process_memory_history.contains(allocation_base)) {
            MemoryRegionHistory<4096> region_history {};
            MemoryEvent event {global_sequence, MemoryEventType::CREATED};
            region_history.allocation_protect = m.AllocationProtect;
            region_history.events.push_back(event);
            process_memory_history[allocation_base] = region_history;
        }

        auto& region_history = process_memory_history[allocation_base];
        region_history.total_region_size += m.RegionSize;

        if (!region_history.block_history.contains(base_address)) {
            BlockHistory<4096> block_history {allocation_base, base_address, {}, {}};
            region_history.block_history[base_address] = block_history;
        }

        auto& block_history = region_history.block_history[base_address];
        MBIHistory mbi {global_sequence, m.State, m.Protect, m.Type, m.RegionSize};
        block_history.mbi_history.push_back(mbi);

        if (m.State == MEM_COMMIT && m.Protect != PAGE_NOACCESS) {
            std::vector<std::array<CacheLine, 4096 / 64>> memory_pages;
            memory_pages.resize(m.RegionSize / 4096);
            ReadProcessMemory(globals::process_handle, current_address, memory_pages.data(), m.RegionSize, 0);

            auto& page_history = block_history.page_history;
            for (uint32_t page_idx = 0; page_idx < m.RegionSize / 4096; page_idx++) {
                auto& page_memory = memory_pages[page_idx];
                uintptr_t page_address = base_address + page_idx * 4096;
                
                if (!page_history.contains(page_address)) {
                    page_history[page_address] = PartiallyPersistentCacheArray<4096> {global_sequence, page_memory};
                    
                    VerifyMemoryAtProcessAddress<kMemoryDebug>(page_address, page_history[page_address].get(global_sequence).data(), 4096);
                } else {
                    // TODO: get the previous page?
                    //page_history[page_address].update(global_sequence, page_memory)
                }
            }
        }
    }
};

}