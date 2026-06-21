#include "memory_manager.h"
#include "fully_persistent_array.h"
#include "globals.h"
#include <basetsd.h>
#include <cstdint>
#include <algorithm>
#include <optional>

namespace {
    // An unordered map of allocation base ptr to the partially persistent memory region history
    std::unordered_map<uintptr_t, chronoporia::MemoryRegionHistory<4096>> process_memory_history;
    
    // If address in blacklist, don't record
    std::vector<uintptr_t> address_blacklist;

    std::vector<chronoporia::PageMemory> GetBlockMemory(const chronoporia::BlockHistory<4096>& block_history, uint64_t block_size, uint32_t target_run_id, uint32_t target_run_sequence) {
        std::vector<chronoporia::PageMemory> rebuilt_block_memory;
        rebuilt_block_memory.resize(block_size / 4096);

        for (const auto& [page_address, persistent_cache_array] : block_history.page_history) {
            // Need the offset since we're looping over an unordered_map
            uint64_t page_offset = (page_address - block_history.base_address) / 4096;
            memcpy(
                rebuilt_block_memory.data() + page_offset, 
                persistent_cache_array.get(target_run_id, target_run_sequence).data(), 
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

void RestoreMemoryAtSequence(uint32_t target_run_id, uint32_t target_run_sequence) {
    // First Free all private allocations
    MEMORY_BASIC_INFORMATION m;
    for (char *address = NULL; VirtualQueryEx(globals::process_handle, address, &m, sizeof(m)) == sizeof(m);
            address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
    {
        if (m.State == MEM_FREE || m.Type == MEM_IMAGE || m.Type == MEM_MAPPED) continue;
        // Skip KUSER_SHARED_DATA
        if (address >= globals::kUserSharedDataBaseAddress && address <= globals::kUserSharedDataEndAddress) continue;
        // Skip blacklisted regions (e.g. the trampoline region) - these are never snapshotted,
        // so freeing them here would leave them with nothing to restore them from in the pass below
        if (std::find(address_blacklist.begin(), address_blacklist.end(), reinterpret_cast<uintptr_t>(m.AllocationBase)) != address_blacklist.end()) continue;
        // TODO: Is this needed? If we free the allocation base this should never trigger? Only act at the allocation base to avoid double-free within the same region
        if (m.BaseAddress != m.AllocationBase) continue;
        // TODO: no need to free TEB.  Do I need to track this in ThreadInfo
        // if (restore_snapshot.thread_contexts.count(m.BaseAddress)) continue;
        FreeMemory(m.AllocationBase);
    }

    for (const auto& [allocation_base, region_history] : process_memory_history) {
        // No event recorded yet along this lineage at this point (region created after
        // target_run_sequence), or the most recent event is freed - either way, skip.
        std::optional<MemoryEventType> event = region_history.events.try_get(target_run_id, target_run_sequence);
        if (!event || *event == MemoryEventType::FREED) continue;

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

            // Block may have been created after target_run_sequence (e.g. by a later snapshot) -
            // skip, there's nothing to restore it to.
            std::optional<MBIHistory> mbi_opt = block_history.mbi_history.try_get(target_run_id, target_run_sequence);
            if (!mbi_opt) continue;
            MBIHistory mbi = *mbi_opt;

            void *base_address_ptr = reinterpret_cast<void *>(block_history.base_address);
            uint64_t block_size = mbi.size;

            std::vector<PageMemory> rebuilt_block = GetBlockMemory(block_history, block_size, target_run_id, target_run_sequence);

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

void SnapshotMemory(uint64_t global_sequence, uint32_t run_id, uint32_t run_sequence) {
    MEMORY_BASIC_INFORMATION m {};

    for (void *address = NULL; VirtualQueryEx(globals::process_handle, address, &m, sizeof(m)) == sizeof(m);
        address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
    {
        // No need to track Free blocks
        if (m.State == MEM_FREE) continue;

        void *current_address = m.BaseAddress;

        // Exclude KUSER_SHARED_DATA — OS-managed read-only page, no need to snapshot
        if (current_address >= globals::kUserSharedDataBaseAddress && current_address <= globals::kUserSharedDataEndAddress) continue;
        
        // TODO: optimize later?
        if (std::find(
            address_blacklist.begin(), 
            address_blacklist.end(), 
            reinterpret_cast<uintptr_t>(current_address)
        ) != address_blacklist.end()) continue;

        uintptr_t allocation_base = reinterpret_cast<uintptr_t>(m.AllocationBase);
        uintptr_t base_address = reinterpret_cast<uintptr_t>(m.BaseAddress);

        // Start a new memory region at each allocation base boundary
        if (current_address == m.AllocationBase && !process_memory_history.contains(allocation_base)) {
            MemoryRegionHistory<4096> region_history {};
            region_history.allocation_protect = m.AllocationProtect;
            region_history.events = FullyPersistentArray<MemoryEventType> {run_id, run_sequence, global_sequence, MemoryEventType::CREATED};
            process_memory_history[allocation_base] = std::move(region_history);
        }

        auto& region_history = process_memory_history[allocation_base];
        region_history.total_region_size += m.RegionSize;

        MBIHistory mbi {m.State, m.Protect, m.Type, m.RegionSize};

        if (!region_history.block_history.contains(base_address)) {
            BlockHistory<4096> block_history {
                allocation_base,
                base_address,
                FullyPersistentArray<MBIHistory> {run_id, run_sequence, global_sequence, mbi},
                {}
            };
            region_history.block_history[base_address] = std::move(block_history);
        } else {
            region_history.block_history[base_address].mbi_history.update(run_id, run_sequence, global_sequence, mbi);
        }

        auto& block_history = region_history.block_history[base_address];

        if (m.State == MEM_COMMIT && m.Protect != PAGE_NOACCESS) {
            std::vector<std::array<CacheLine, 4096 / 64>> memory_pages;
            memory_pages.resize(m.RegionSize / 4096);
            ReadProcessMemory(globals::process_handle, current_address, memory_pages.data(), m.RegionSize, 0);

            auto& page_history = block_history.page_history;
            for (uint32_t page_idx = 0; page_idx < m.RegionSize / 4096; page_idx++) {
                auto& page_memory = memory_pages[page_idx];
                uintptr_t page_address = base_address + page_idx * 4096;
                
                if (!page_history.contains(page_address)) {
                    page_history[page_address] = FullyPersistentCacheArray<4096> {run_id, run_sequence, global_sequence, page_memory};
                } else {
                    // O(1) read of the last-materialized page contents instead of reconstructing it
                    // via find_fat_node() per cache line - see FullyPersistentCacheArray::latest().
                    const PageMemory& previous_page = page_history[page_address].latest();

                    for (uint32_t cache_line_idx = 0; cache_line_idx < 4096 / 64; cache_line_idx++) {
                        if (memcmp(previous_page.data() + cache_line_idx * 64, page_memory[cache_line_idx].data(), 64) == 0) continue;

                        page_history[page_address].update(run_id, run_sequence, global_sequence, cache_line_idx, page_memory[cache_line_idx]);
                    }
                }
            }
        }
    }
};

void AddBlacklistAddress(uintptr_t address) {
    address_blacklist.push_back(address);
}

}