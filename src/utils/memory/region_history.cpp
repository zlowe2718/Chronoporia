#include "region_history.h"
#include <optional>
#include <Windows.h>

namespace chronoporia {

    std::optional<RegionInfo> RegionHistory::get_current_region_info(const uint32_t target_run_id, const uint32_t target_run_seq) {
        RegionInfo region_info {};

        auto mbi = mbi_history_.GetState(target_run_id, target_run_seq);
        if (!mbi) return std::nullopt;

        region_info.current_mbi = mbi.value();
        // Only get the memory if committed
        if (mbi.value().State == MEM_COMMIT) {
            region_info.current_memory = memory_history_.get_region_at_time(target_run_id, target_run_seq, mbi.value().RegionSize);
        }

        return region_info;
    }

    void RegionHistory::update(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq, MEMORY_BASIC_INFORMATION&& mbi, std::span<const std::byte> region_buf) {
        memory_history_.update(run_id, run_seq, global_seq, region_buf);
        mbi_history_.AddChild(std::move(mbi), run_id, run_seq, global_seq);
    }

    void RegionHistory::make_branch(const uint32_t from_run_id, uint32_t from_run_sequence, uint32_t to_run_id) {
        memory_history_.make_branch(from_run_id, from_run_sequence, to_run_id);
        mbi_history_.RevertToState(from_run_id, from_run_sequence, to_run_id);
    }
}