#pragma once

#include "allocation_region.h"
#include <Windows.h>
#include <cstdint>
#include <unordered_map>
#include <span>

using AllocationBase = uintptr_t;

namespace chronoporia {

    class ProcessMemoryHistory {
    public:
        std::unordered_map<AllocationBase, AllocationRegion> process_history_;

        
        // Loops through process_history to find the regions that currently aren't freed
        // Returns a vector of current allocation regions
        std::vector<AllocationInfo> get_current_allocation_regions(const uint32_t target_run_id, const uint32_t target_run_seq);

        // Update all the region's memory
        void update(MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_memory, const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq);

        // This loops through process_history's allocation addresses and if they're not in seen_allocation_bases updates the allocation region to freed
        void update_stale_regions(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq);

        // This makes alll the branches for AllocationRegion and its RegionHistory data structures.
        void make_branch(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id);
    };    

}