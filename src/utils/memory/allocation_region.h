#pragma once

#include "region_history.h"
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <span>

namespace chronoporia {

    struct AllocationInfo {
        uintptr_t allocation_base;
        std::vector<RegionInfo> all_regions;
    };

    class AllocationRegion {
    public:
        uintptr_t allocation_base_;
        std::vector<RegionHistory> regions_history_;

        AllocationRegion(MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_memory, uint32_t run_id, uint32_t run_seq, uint64_t global_seq) 
            : allocation_base_ {reinterpret_cast<uintptr_t>(mbi.AllocationBase)}
        {
            auto initial_region = RegionHistory {mbi, region_memory, run_id, run_seq, global_seq};
            regions_history_.push_back(std::move(initial_region));
        }

        // This loops through all the regions in regions_history to find the correct RegionHistory at this time.
        // This will be in order where the top region is the allocation_base region
        std::vector<RegionInfo> get_current_regions(const uint32_t target_run_id, const uint32_t target_run_seq);
    
        // Update region with the mbi and process memory
        void update(MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_memory, const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq);

        // This loops through regions_history's base addresses and if they're not in seen_base_addresses updates the region history to freed
        // If process memory never saw this region then seen_base_addresses is empty and everything will be marked as free
        void update_stale_regions(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq);

        // This makes alll the branches for AllocationRegion and its RegionHistory data structures
        void make_branch(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id);

        bool is_allocation_freed(const uint32_t target_run_id, const uint32_t target_run_seq);
    private:
        std::vector<uintptr_t> seen_base_addresses_;        
    };

}