#include "process_memory_history.h"
#include "allocation_region.h"
#include <cstdint>

namespace chronoporia {

    std::vector<AllocationInfo> ProcessMemoryHistory::get_current_allocation_regions(const uint32_t target_run_id, const uint32_t target_run_seq) {
        std::vector<AllocationInfo> allocation_regions = {};
        for (auto& [alloc_base, allocation_region] : process_history_) {
            if(allocation_region.is_allocation_freed(target_run_id, target_run_seq)) continue;

            auto region_info = allocation_region.get_current_regions(target_run_id, target_run_seq);
            allocation_regions.push_back(AllocationInfo {alloc_base, std::move(region_info)});
        }
        return allocation_regions;
    }

    void ProcessMemoryHistory::update(MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_memory, const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq) {
        uintptr_t allocation_base = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
        if (!process_history_.contains(allocation_base)) {
            process_history_.try_emplace(allocation_base, AllocationRegion {std::move(mbi), region_memory, run_id, run_seq, global_seq});
        } else {
            process_history_.at(allocation_base).update(std::move(mbi), region_memory, run_id, run_seq, global_seq);
        }
    }


    void ProcessMemoryHistory::update_stale_regions(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq) {
        for (auto& [alloc_base, allocation_region] : process_history_) {
            allocation_region.update_stale_regions(run_id, run_seq, global_seq);
        }
    }

    void ProcessMemoryHistory::make_branch(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id) {
        for (auto& [alloc_base, allocation_region] : process_history_) {
            allocation_region.make_branch(from_run_id, from_run_seq, to_run_id);
        }
    }
}