#include "allocation_region.h"
#include <algorithm>
#include <winnt.h>

namespace chronoporia {

    void AllocationRegion::update(MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_memory, const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq) {
        seen_base_addresses_.push_back(reinterpret_cast<uintptr_t>(mbi.BaseAddress));

        auto itr = std::find_if(regions_history_.begin(), regions_history_.end(), 
            [mbi](const auto& region_history) {
                return region_history.base_address_ == reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            }
        );

        // make a new history
        if (itr == regions_history_.end()) {
            auto new_history = RegionHistory {std::move(mbi), region_memory, run_id, run_seq, global_seq};
            regions_history_.push_back(std::move(new_history));
            return;
        }
        
        auto index = itr - regions_history_.begin();
        regions_history_[index].update(run_id, run_seq, global_seq, std::move(mbi), region_memory);
    }

    std::vector<RegionInfo> AllocationRegion::get_current_regions(const uint32_t target_run_id, const uint32_t target_run_seq) {
        std::vector<RegionInfo> regions;
        for (auto& region_history : regions_history_) {
            auto region_info = region_history.get_current_region_info(target_run_id, target_run_seq);
            // if doesn't exist or the current state is freed skip this region
            if (!region_info || region_info->current_mbi.State == MEM_FREE) continue;

            regions.push_back(region_info.value());
        }
        return regions;
    }

    void AllocationRegion::update_stale_regions(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq) {
        if (regions_history_.size() == seen_base_addresses_.size()) {
            seen_base_addresses_.clear();
            return;
        };

        for (auto& region_history : regions_history_) {
            auto itr = std::find(seen_base_addresses_.begin(), seen_base_addresses_.end(), region_history.base_address_);
            if (itr != seen_base_addresses_.end()) continue;

            //if not found then update with a MEM_FREE
            MEMORY_BASIC_INFORMATION mbi = {};
            mbi.State = MEM_FREE;
            region_history.update(run_id, run_seq, global_seq, std::move(mbi), std::vector<std::byte>());
        }

        seen_base_addresses_.clear();
    }

    void AllocationRegion::make_branch(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id) {
        for (auto& region_history : regions_history_) {
            region_history.make_branch(from_run_id, from_run_seq, to_run_id);
        }
    }

    bool AllocationRegion::is_allocation_freed(const uint32_t target_run_id, const uint32_t target_run_seq) {
        auto& region_history = regions_history_[0];
        auto allocation_region_info = region_history.get_current_region_info(target_run_id, target_run_seq);
        if(!allocation_region_info) return true;

        return allocation_region_info->current_mbi.State == MEM_FREE;
    }
}