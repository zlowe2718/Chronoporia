#include "memory_manager.h"
#include "process_memory_history.h"
#include "globals.h"
#include "memory_utils.h"
#include <Windows.h>
#include <cstdint>
#include <numeric>
#include <winnt.h>

namespace {
    chronoporia::ProcessMemoryHistory process_memory_history;

    // If address in blacklist, don't record.  We won't have many of these so a vector is better
    std::vector<uintptr_t> address_blacklist;
}

namespace chronoporia {

    void SnapshotMemory(uint64_t global_sequence, uint32_t run_id, uint32_t run_sequence) {
        MEMORY_BASIC_INFORMATION m;

        for (void *address = nullptr; VirtualQueryEx(globals::process_handle, address, &m, sizeof(m)) == sizeof(m);
            address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
        {
            void *current_address = m.BaseAddress;

            // Exclude KUSER_SHARED_DATA — OS-managed read-only page, no need to snapshot
            if (current_address >= globals::kUserSharedDataBaseAddress && current_address <= globals::kUserSharedDataEndAddress) continue;

            // Ignore addresses in the blacklist
            // TODO: optimize later?
            if (std::find(
                address_blacklist.begin(), 
                address_blacklist.end(), 
                reinterpret_cast<uintptr_t>(current_address)
            ) != address_blacklist.end()) continue;
            
            std::vector<std::byte> region_memory;
            // If the state is committed then get the memory.  Do not save freed or reserved memory
            if (m.State == MEM_COMMIT) {
                region_memory.resize(m.RegionSize);
                ReadProcessMemory(globals::process_handle, current_address, region_memory.data(), m.RegionSize, nullptr);
            }

            process_memory_history.update(std::move(m), region_memory, run_id, run_sequence, global_sequence);
        }

        // update any stale regions that we didn't see during our scan
        process_memory_history.update_stale_regions(run_id, run_sequence, global_sequence);
    }

    void RestoreMemoryAtSequence(uint32_t target_run_id, uint32_t target_run_sequence) {
        // Can't free the TEB but we can ovewrite it, TODO: skip the TEB address here too
        MEMORY_BASIC_INFORMATION m;

        // Free all addresses that are easy to free as it makes restoration easier
        for (char *address = nullptr; VirtualQueryEx(globals::process_handle, address, &m, sizeof(m)) == sizeof(m);
                address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
        {
            if (m.State == MEM_FREE || m.Type == MEM_IMAGE || m.Type == MEM_MAPPED) continue;
            if (address >= globals::kUserSharedDataBaseAddress && address <= globals::kUserSharedDataEndAddress) continue;
            if (std::find(address_blacklist.begin(), address_blacklist.end(), reinterpret_cast<uintptr_t>(m.AllocationBase)) != address_blacklist.end()) continue;
            virtual_free(m.AllocationBase);
        }

        auto current_allocation_regions = process_memory_history.get_current_allocation_regions(target_run_id, target_run_sequence);

        // Now loop through all allocationr regions that are alive at this time and either create and dump the memory
        // or replace (overwrite) memory that wasn't freed from above
        for (const auto& allocation_region : current_allocation_regions) {
            const auto& current_sub_regions = allocation_region.all_regions;
            uint64_t total_region_size = std::accumulate(current_sub_regions.begin(), current_sub_regions.end(), uint64_t{0},
                [](uint64_t sum, const auto& region) {
                    return sum + region.current_mbi.RegionSize;
                }
            );

            // First we need to see if we're writing to freed memory or overwriting existing memory
            void *allocation_base = reinterpret_cast<void *>(allocation_region.allocation_base);
            VirtualQueryEx(globals::process_handle, allocation_base, &m, sizeof(m));
            bool overwriting_existing = m.State != MEM_FREE;

            // If we're not overwriting then we need to reserve the memory the entire allocation region first
            if (!overwriting_existing) {
                virtual_reserve_memory(allocation_base, total_region_size, current_sub_regions[0].current_mbi.AllocationProtect);             
            }

            // Now rebuild each sub region in order
            for (const auto& current_sub_region : current_sub_regions) {
                const auto& current_mbi = current_sub_region.current_mbi;
                auto region_size = current_mbi.RegionSize;
                auto base_address = current_mbi.BaseAddress;
                auto protect = current_mbi.Protect;

                if (current_mbi.State != MEM_COMMIT) continue;

                if (!overwriting_existing) {
                    // if not overwriting then we can reserve as execute readwrite (we fix the protection later)
                    virtual_alloc_full_permissions(base_address, region_size);
                } else if (current_mbi.Protect & PAGE_GUARD) {
                    // This is the Thread Stack memory blocks
                    virtual_protect(base_address, region_size, protect & ~PAGE_GUARD);
                } else if (current_mbi.Type == MEM_IMAGE) {
                    // This is the DLLs.  We're remapping to WriteCopy so that we don't mess with the underlying dll memory for other programs sharing it
                    // This can also be mapped files
                    virtual_protect(base_address, region_size, PAGE_EXECUTE_WRITECOPY);
                }
                virtual_write(current_mbi.BaseAddress, current_sub_region.current_memory);
                virtual_protect(base_address, region_size, protect); 
            }
        } 
    }

    void AddBlacklistAddress(uintptr_t address) {
        address_blacklist.push_back(address);
    }

    void CreateMemoryHistoryBranch(uint32_t from_run_id, uint32_t from_run_seq, uint32_t to_run_id) {
        process_memory_history.make_branch(from_run_id, from_run_seq, to_run_id);
    }
}