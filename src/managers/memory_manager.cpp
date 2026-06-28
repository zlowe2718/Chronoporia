#include "memory_manager.h"
#include "fully_persistent_array.h"
#include "globals.h"
#include "quill/LogMacros.h"
#include <basetsd.h>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <set>

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

    // region_history.total_region_size only ever grows (SnapshotMemory adds to it the first time
    // each sub-block is discovered, and never removes from it), so it reflects the region's *final*
    // size across its whole recorded lifetime, not its size at (target_run_id, target_run_sequence).
    // Restoring an earlier snapshot with that size asks VirtualAllocEx to reserve more address space
    // than was actually reserved at that point, which collides with whatever now lives past the end
    // of the smaller, real reservation. Recompute the size from the blocks that actually existed at
    // the target snapshot instead.
    uint64_t ComputeRegionSizeAtSequence(
        const chronoporia::MemoryRegionHistory<4096>& region_history,
        uintptr_t allocation_base,
        uint32_t target_run_id,
        uint32_t target_run_sequence
    ) {
        uint64_t region_size = 0;
        for (const auto& [base_address, block_history] : region_history.block_history) {
            std::optional<chronoporia::MBIHistory> mbi_opt = block_history.mbi_history.try_get(target_run_id, target_run_sequence);
            if (!mbi_opt) continue;

            uint64_t block_end_offset = (base_address - allocation_base) + mbi_opt->size;
            if (block_end_offset > region_size) region_size = block_end_offset;
        }
        return region_size;
    }

    void inline FreeMemory(void *allocation_base) {
        if (!VirtualFreeEx(globals::process_handle, allocation_base, 0, MEM_RELEASE))
        {
            LOG_DEBUG(globals::logger, "VirtualFreeEx failed:\n"
                "    error:    {}\n"
                "    address:  {:p}", GetLastError(), allocation_base);
        }
    }

    // Logs what, if anything, currently occupies an address whose VirtualAllocEx/VirtualProtectEx/
    // WriteProcessMemory call just failed - lets us tell "address collided with something else
    // that now lives there" apart from other failure causes.
    void inline LogCurrentOccupant(void *address) {
        MEMORY_BASIC_INFORMATION occupant {};
        if (VirtualQueryEx(globals::process_handle, address, &occupant, sizeof(occupant)) != sizeof(occupant)) {
            LOG_DEBUG(globals::logger,"    occupant: <VirtualQueryEx failed, error {}>", GetLastError());
            return;
        }
        LOG_DEBUG(globals::logger,"    occupant: base={:p} alloc_base={:p} region_size={} state=0x{:x} protect=0x{:x} alloc_protect=0x{:x} type=0x{:x}",
            occupant.BaseAddress, occupant.AllocationBase, (unsigned long long)occupant.RegionSize,
            occupant.State, occupant.Protect, occupant.AllocationProtect, occupant.Type);
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
        if (!event || *event == MemoryEventType::FREED) {
            continue;
        }

        VirtualQueryEx(globals::process_handle, reinterpret_cast<void *>(allocation_base), &m, sizeof(m));

        bool overwrite_address = m.State != MEM_FREE;

        // If the memory is already freed then we need to reserve the memory before we can write to it
        if (!overwrite_address) {
            uint64_t region_size_at_target = ComputeRegionSizeAtSequence(region_history, allocation_base, target_run_id, target_run_sequence);
            if (!VirtualAllocEx(
                globals::process_handle,
                reinterpret_cast<void *>(allocation_base),
                region_size_at_target,
                MEM_RESERVE,
                region_history.allocation_protect
            ))
            {
                LOG_DEBUG(globals::logger,"VirtualAllocEx (MEM_RESERVE) failed:\n"
                    "    error:    %ld\n"
                    "    address:  %p\n"
                    "    size:     %lld\n", GetLastError(), reinterpret_cast<void *>(allocation_base), region_size_at_target);
                LogCurrentOccupant(reinterpret_cast<void *>(allocation_base));
            }
        }

        for (const auto& [base_address, block_history] : region_history.block_history) {
            DWORD old_protect;
            SIZE_T bytes_written;

            // Block hasn't been created yet as of this target, or has since been retired -
            // absorbed into a different block when an MBI boundary shifted (e.g. a thread
            // stack's guard page moving down on growth), or actually freed/decommitted. Either
            // way it must not be reapplied: mbi_history alone can't tell "stale" apart from
            // "current" since it just keeps returning the last value it was ever given.
            std::optional<MemoryEventType> block_event = block_history.events.try_get(target_run_id, target_run_sequence);
            if (!block_event || *block_event == MemoryEventType::FREED) {
                continue;
            }

            // Block may have been created after target_run_sequence (e.g. by a later snapshot) -
            // skip, there's nothing to restore it to.
            std::optional<MBIHistory> mbi_opt = block_history.mbi_history.try_get(target_run_id, target_run_sequence);
            if (!mbi_opt) {
                continue;
            }
            MBIHistory mbi = *mbi_opt;

            void *base_address_ptr = reinterpret_cast<void *>(block_history.base_address);
            uint64_t block_size = mbi.size;

            if (mbi.state == MEM_COMMIT)
            {
                // Page contents are only meaningful (and only ever read) for committed blocks -
                // a MEM_RESERVE-only block can have a RegionSize in the hundreds of GB/TB, and
                // resizing rebuilt_block_memory for one of those throws std::bad_alloc for no
                // benefit, since nothing below this branch uses it.
                std::vector<PageMemory> rebuilt_block = GetBlockMemory(block_history, block_size, target_run_id, target_run_sequence);

                // Only need to commit fresh pages — in overwrite mode the page is already committed
                // (image/mapped pages, or private pages we kept in pass 1). Calling VirtualAllocEx
                // on an already-committed page would fail, so skip it.
                if (!overwrite_address)
                {
                    if (!VirtualAllocEx(globals::process_handle, base_address_ptr, block_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                    {
                        LOG_DEBUG(globals::logger,"VirtualAllocEx (MEM_COMMIT) failed:\n"
                            "    error:    {}\n"
                            "    address:  {:p}\n"
                            "    size:     {}", GetLastError(), base_address_ptr, block_size);
                        LogCurrentOccupant(base_address_ptr);
                    }
                } else if (mbi.protect & PAGE_GUARD) {
                    // This is the Thread Stack memory blocks
                    if (!VirtualProtectEx(globals::process_handle, base_address_ptr, block_size, mbi.protect & ~PAGE_GUARD, &old_protect))
                    {
                        LOG_DEBUG(globals::logger,"VirtualProtectEx (TEB) failed:\n"
                            "    error:    {}\n"
                            "    address:  {:p}\n"
                            "    size:     {}", GetLastError(), base_address_ptr, block_size);
                        LogCurrentOccupant(base_address_ptr);
                    }
                } else if (mbi.type == MEM_IMAGE) {
                    // This is the DLLs.  We're remapping to WriteCopy so that we don't mess with the underlying dll memory for other programs sharing it
                    // This can also be mapped files
                    if (!VirtualProtectEx(globals::process_handle, base_address_ptr, block_size, PAGE_EXECUTE_WRITECOPY, &old_protect))
                    {
                        LOG_DEBUG(globals::logger,"VirtualProtectEx (IMAGE) failed:\n"
                            "    error:    {}\n"
                            "    address:  {:p}\n"
                            "    size:     {}", GetLastError(), base_address_ptr, block_size);
                        LogCurrentOccupant(base_address_ptr);
                    }
                }
                if (!WriteProcessMemory(globals::process_handle, base_address_ptr, rebuilt_block.data(), block_size, &bytes_written))
                {
                    LOG_DEBUG(globals::logger,"WriteProcessMemory failed:\n"
                        "    error:    {}\n"
                        "    address:  {:p}\n"
                        "    size:     {}", GetLastError(), base_address_ptr, block_size);
                    LogCurrentOccupant(base_address_ptr);
                }
                if (!VirtualProtectEx(globals::process_handle, base_address_ptr, block_size, mbi.protect, &old_protect))
                {
                    LOG_DEBUG(globals::logger,"VirtualProtectEx failed:\n"
                        "    error:    {}\n"
                        "    address:  {:p}\n"
                        "    size:     {}", GetLastError(), base_address_ptr, block_size);
                    LogCurrentOccupant(base_address_ptr);
                }
            }        
        }
    }
};

void SnapshotMemory(uint64_t global_sequence, uint32_t run_id, uint32_t run_sequence) {
    MEMORY_BASIC_INFORMATION m {};
    std::set<uintptr_t> live_allocation_bases;
    // allocation_base -> set of base_address values seen as their own MBI region this pass.
    std::unordered_map<uintptr_t, std::set<uintptr_t>> live_block_addresses;

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

        live_allocation_bases.insert(allocation_base);
        live_block_addresses[allocation_base].insert(base_address);

        // Start a new memory region at each allocation base boundary
        if (current_address == m.AllocationBase) {
            if (!process_memory_history.contains(allocation_base)) {
                MemoryRegionHistory<4096> region_history {};
                region_history.allocation_protect = m.AllocationProtect;
                region_history.events = FullyPersistentArray<MemoryEventType> {run_id, run_sequence, global_sequence, MemoryEventType::CREATED};
                process_memory_history[allocation_base] = std::move(region_history);
            } else {
                auto& existing_region_history = process_memory_history[allocation_base];

                // allocation_base values get reused - the OS is free to hand a freed VA range
                // back out to a brand new, completely unrelated VirtualAlloc (this is routine
                // for the process heap, which constantly reserves/frees/re-reserves segments).
                // Without this, a region that was FREED here would stay FREED forever in its
                // event lineage, so RestoreMemoryAtSequence would skip restoring it for any
                // later target even though it's actually live - leaving that address range
                // unmapped after restore instead of holding the new allocation's data, which is
                // exactly the kind of corruption that shows up as heap metadata getting
                // clobbered. allocation_protect isn't itself historized, so it's refreshed here
                // too in case the new allocation's protection differs from the old one's.
                std::optional<MemoryEventType> latest_region_event = existing_region_history.events.try_get(run_id, run_sequence);
                if (!latest_region_event || *latest_region_event == MemoryEventType::FREED) {
                    existing_region_history.allocation_protect = m.AllocationProtect;
                    existing_region_history.events.update(run_id, run_sequence, global_sequence, MemoryEventType::CREATED);
                }
            }
        }

        auto& region_history = process_memory_history[allocation_base];

        MBIHistory mbi {m.State, m.Protect, m.Type, m.RegionSize};

        if (!region_history.block_history.contains(base_address)) {
            // Only count this sub-block's size the first time it's discovered - it stays
            // part of the same fixed-size reservation on every later snapshot, so re-adding
            // it each pass would make total_region_size grow without bound.
            region_history.total_region_size += m.RegionSize;

            BlockHistory<4096> block_history {
                allocation_base,
                base_address,
                FullyPersistentArray<MemoryEventType> {run_id, run_sequence, global_sequence, MemoryEventType::CREATED},
                FullyPersistentArray<MBIHistory> {run_id, run_sequence, global_sequence, mbi},
                {}
            };
            region_history.block_history[base_address] = std::move(block_history);
        } else {
            auto& existing_block_history = region_history.block_history[base_address];

            // This address range previously belonged to a different block (e.g. it was merged
            // away by an earlier snapshot - see BlockHistory::events) and is now back to being
            // its own MBI region - re-mark it live so RestoreMemoryAtSequence doesn't skip it.
            std::optional<MemoryEventType> latest_block_event = existing_block_history.events.try_get(run_id, run_sequence);
            if (!latest_block_event || *latest_block_event == MemoryEventType::FREED) {
                existing_block_history.events.update(run_id, run_sequence, global_sequence, MemoryEventType::CREATED);
            }

            existing_block_history.mbi_history.update(run_id, run_sequence, global_sequence, mbi);
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

    // Any allocation_base we've tracked before but didn't see in this scan has been freed -
    // record that so RestoreMemoryAtSequence (memory_manager.cpp) can skip it for any later
    // target snapshot, instead of trying to recreate it on top of whatever now occupies that
    // address range.
    for (auto& [allocation_base, region_history] : process_memory_history) {
        if (live_allocation_bases.contains(allocation_base)) continue;

        std::optional<MemoryEventType> latest_event = region_history.events.try_get(run_id, run_sequence);
        if (latest_event && *latest_event == MemoryEventType::FREED) continue;

        region_history.events.update(run_id, run_sequence, global_sequence, MemoryEventType::FREED);
    }

    // Any base_address we've tracked before but didn't see as its own MBI region this pass has
    // been retired - either it was absorbed into a different block when an MBI boundary shifted
    // (e.g. a thread stack's guard page moving down on growth merges the old guard page into the
    // committed region above it), or the allocation it belonged to was freed outright. Either
    // way, record it so RestoreMemoryAtSequence stops treating this stale BlockHistory entry as
    // live for any later target snapshot - otherwise it can get blindly reapplied over whatever
    // now occupies the same address range.
    for (auto& [allocation_base, region_history] : process_memory_history) {
        auto live_blocks_it = live_block_addresses.find(allocation_base);
        const std::set<uintptr_t> empty_set;
        const std::set<uintptr_t>& live_blocks = live_blocks_it != live_block_addresses.end() ? live_blocks_it->second : empty_set;

        for (auto& [base_address, block_history] : region_history.block_history) {
            if (live_blocks.contains(base_address)) continue;

            std::optional<MemoryEventType> latest_block_event = block_history.events.try_get(run_id, run_sequence);
            if (latest_block_event && *latest_block_event == MemoryEventType::FREED) continue;

            block_history.events.update(run_id, run_sequence, global_sequence, MemoryEventType::FREED);
        }
    }
};

void AddBlacklistAddress(uintptr_t address) {
    address_blacklist.push_back(address);
}

// FullyPersistentArray/FullyPersistentCacheArray lookups only see history recorded under
// new_run_id itself unless this is called - without it, anything created once under
// target_run_id and never touched again (which is most of a process's memory: the main
// thread's stack, interpreter-lifetime heap, etc.) is invisible to RestoreMemoryAtSequence
// for the new run, and gets freed and never recreated on restore.
void CreateMemoryHistoryBranch(uint32_t target_run_id, uint32_t target_run_seq, uint32_t new_run_id) {
    for (auto& [allocation_base, region_history] : process_memory_history) {
        region_history.events.register_branch(new_run_id, target_run_id, target_run_seq);

        for (auto& [base_address, block_history] : region_history.block_history) {
            block_history.events.register_branch(new_run_id, target_run_id, target_run_seq);
            block_history.mbi_history.register_branch(new_run_id, target_run_id, target_run_seq);

            for (auto& [page_address, page_cache] : block_history.page_history) {
                page_cache.register_branch(new_run_id, target_run_id, target_run_seq);
            }
        }
    }
}

}