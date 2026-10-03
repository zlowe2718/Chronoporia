#pragma once

#include "execution_tree.h"
#include "fully_persistent_region.h"
#include <Windows.h>
#include <cstdint>



namespace chronoporia {

    struct RegionInfo {
        MEMORY_BASIC_INFORMATION current_mbi;
        std::vector<std::byte> current_memory;
    };

    class RegionHistory {
    public:
        uintptr_t base_address_;
        ExecutionTree<MEMORY_BASIC_INFORMATION> mbi_history_;
        FullyPersistentRegion memory_history_;

        // Starts the region history and creates the first node for the execution tree and fully persistent region
        RegionHistory(const MEMORY_BASIC_INFORMATION& mbi, std::span<const std::byte> region_buf, uint32_t run_id, uint32_t run_seq, uint64_t global_seq)
            : base_address_ {reinterpret_cast<uintptr_t>(mbi.BaseAddress)}
            , mbi_history_ {mbi, run_id, run_seq, global_seq}
            , memory_history_(run_id, run_seq, global_seq, static_cast<uint32_t>(region_buf.size()))
        {
            memory_history_.update(run_id, run_seq, global_seq, region_buf);
        }

        // Gets the current mbi and the memory
        // Returns nullopt if not found so the caller knows if the region exists at this time 
        std::optional<RegionInfo> get_current_region_info(const uint32_t target_run_id, const uint32_t target_run_seq);

        // Update the region history with the new memory and mbi
        void update(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq, MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_buf);

        // Make a new branch for full persistence
        void make_branch(const uint32_t from_run_id, uint32_t from_run_sequence, uint32_t to_run_id);
    };


}