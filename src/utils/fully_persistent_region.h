#pragma once

#include "globals.h"
#include "quill/LogMacros.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <Windows.h>

namespace chronoporia {

using RunId = uint32_t;
using RunSeq = uint32_t;
using AllocationBase = uintptr_t;
using BaseAddress = uintptr_t;
using PageLine = unsigned char[4096];

static constexpr uint16_t kPageLineSize = 4096;

struct FatNode {
    uint32_t run_id;
    uint32_t run_seq;
    uint64_t global_seq;
    PageLine cache_line_memory;
};

class FatNodeHistory {
public:
    std::unordered_map<RunId, std::vector<FatNode>> fat_node_branches_;
    std::unordered_map<RunId, std::pair<std::optional<RunId>, std::optional<RunSeq>>> branch_history_;
    uint32_t created_run_id_;
    uint32_t created_run_sequence_;

    FatNodeHistory() = delete;

    FatNodeHistory(const uint32_t run_id, const uint32_t run_seq) 
        : fat_node_branches_ {}
        , branch_history_ {}
        , created_run_id_ {run_id}
        , created_run_sequence_ {run_seq}
        , current_run_id_ {std::nullopt}
        , current_run_idx_ {std::nullopt}
    {}

    // Gets the fat node closest (<=) to the run_id and run sequence. Also sets the current run id and run idx
    // Returns nullopt if data doesn't exist at the target
    std::optional<FatNode> get_node_nearest(const uint32_t target_run_id, const uint32_t target_run_sequence) {
        // if the target is before this was created then there are no nodes
        if (target_run_id < created_run_id_ || (target_run_id == created_run_id_ && target_run_sequence < created_run_sequence_)) {
            current_run_id_ = std::nullopt;
            current_run_idx_ = std::nullopt;
            return std::nullopt;
        }
        // This should only happen if something wasn't called correctly
        if (!fat_node_branches_.contains(target_run_id)) {
            LOG_ERROR(globals::logger, "run_id: {} not found in FatNodeHistory.  Was a branch not made?", target_run_id); 
            current_run_id_ = std::nullopt;
            current_run_idx_ = std::nullopt;
            return std::nullopt;
        }

        // we checked to ensure a node exists so this is safe
        uint32_t next_run_id = target_run_id;
        uint32_t next_run_sequence = target_run_sequence;
        while (true) {
            const auto& fat_branch = fat_node_branches_.at(next_run_id);
            
            // If no data then hop up to the parent branch
            if (fat_branch.size() == 0) {
                auto run_id_check = branch_history_[next_run_id].first;
                if (!run_id_check) {
                    LOG_ERROR(globals::logger, "No parent found.  Was a branch not made?"); 
                    current_run_id_ = std::nullopt;
                    current_run_idx_ = std::nullopt;
                    return std::nullopt;
                }
                next_run_sequence = branch_history_[next_run_id].second.value();
                next_run_id = branch_history_[next_run_id].first.value();
                continue;
            }
            auto itr = std::upper_bound(fat_branch.begin(), fat_branch.end(), next_run_sequence,
                [](const uint32_t run_seq, const auto& fat_node) {
                    return run_seq < fat_node.run_seq;
            });
            
            if (itr == fat_branch.begin()) {
                // no node found, go up the parent chain
                auto run_id_check = branch_history_[next_run_id].first;
                if (!run_id_check) {
                    LOG_ERROR(globals::logger, "No parent found.  Was a branch not made?"); 
                    current_run_id_ = std::nullopt;
                    current_run_idx_ = std::nullopt;
                    return std::nullopt;
                }
                next_run_sequence = branch_history_[next_run_id].second.value();
                next_run_id = branch_history_[next_run_id].first.value();
                continue;                
            }

            itr -= 1; 
            current_run_id_ = next_run_id;
            current_run_idx_ = static_cast<uint32_t>(itr - fat_branch.begin());

            return *itr;
        }
    }

    void update(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq, PageLine& region_buf) {
        if (current_run_id_) {
            // there's a nearest node so compare against the previous
            const auto& previous_node = fat_node_branches_[current_run_id_.value()][current_run_idx_.value()];

            // If memory is the same then don't update
            if (memcmp(previous_node.cache_line_memory, &region_buf, kPageLineSize) == 0) return;
            if (current_run_id_ == run_id) {
                current_run_idx_ = current_run_idx_.value() + 1;
            } else {
                current_run_idx_ = 0;
            }
            current_run_id_ = run_id;
        } else {
            current_run_id_ = run_id;
            current_run_idx_ = 0;
        }

        FatNode new_node = {run_id, run_seq, global_seq, 0};
        memcpy(new_node.cache_line_memory, region_buf, kPageLineSize);
        fat_node_branches_[current_run_id_.value()].push_back(std::move(new_node));
    }

    // On New branch find the closest node and set the current_run_id and run_idx to that node (run_idx is the vector idx)
    void make_branch(const uint32_t from_run_id, const uint32_t from_run_sequence, const uint32_t to_run_id) {
        auto node = get_node_nearest(from_run_id, from_run_sequence);
        if (!node) {
            branch_history_.insert({to_run_id, {std::nullopt, std::nullopt}});
        } else {
            branch_history_.insert({to_run_id, {from_run_id, from_run_sequence}});
        }
        fat_node_branches_.insert({to_run_id, {}});
    }
private:
    // tracking indexes for faster lookups when appending new data
    std::optional<uint32_t> current_run_id_;
    std::optional<uint32_t> current_run_idx_;
};

class FullyPersistentRegion {
public:
    uint32_t created_run_id_;
    uint32_t created_run_seq_;
    uint64_t created_global_seq_;
    std::vector<FatNodeHistory> region_history_;

    FullyPersistentRegion() = delete;

    FullyPersistentRegion(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq, const uint32_t initial_region_size)
        : created_run_id_ {run_id}
        , created_run_seq_ {run_seq}
        , created_global_seq_ {global_seq} 
    {
        region_history_.reserve(initial_region_size / kPageLineSize);
    }

    void update(const uint32_t run_id, const uint32_t run_seq, const uint64_t global_seq, std::span<const std::byte> region_buf) {
        // Starting at the beginning of the region_history vector every time is safe because if the base_address doesn't line up then
        //  the region would be considered "freed" and a new Region would be made
        // change view length to 64 byte aligned
        std::span<PageLine> cache_line_memory {
            reinterpret_cast<PageLine*>(const_cast<std::byte *>(region_buf.data())), 
            region_buf.size() / sizeof(PageLine)
        };

        uint32_t i = 0;
        uint32_t min_size = std::min(region_history_.size(), cache_line_memory.size());
        // safely update all common nodes
        while (i < min_size) {
            region_history_[i].update(run_id, run_seq, global_seq, cache_line_memory[i]);
            i++;
        }

        // if incoming cache line is larger then make new fat nodes
        if (cache_line_memory.size() > region_history_.size()) {
            while (i < cache_line_memory.size()) {
                FatNodeHistory new_history = {run_id, run_seq};
                new_history.update(run_id, run_seq, global_seq, cache_line_memory[i]);
                region_history_.push_back(std::move(new_history));
                i++;
            }
        }
    }

    void make_branch(const uint32_t from_run_id, uint32_t from_run_sequence, uint32_t to_run_id) {
        for (auto& fate_node_history : region_history_) {
            fate_node_history.make_branch(from_run_id, from_run_sequence, to_run_id);
        }
    }

    // If node doesn't exist then leave the memory empty.  This should be handled by the caller to ensure nodes are valid
    std::vector<std::byte> get_region_at_time(const uint32_t from_run_id, const uint32_t from_run_sequence, uint32_t region_size) {
        std::vector<std::byte> region;
        uint32_t max_idx = region_size / kPageLineSize;
        region.resize(region_size);
        for (uint32_t i = 0; i < max_idx; i++) {
            auto fat_node = region_history_[i].get_node_nearest(from_run_id, from_run_sequence);
            if (!fat_node) continue;
            memcpy(&region[i * kPageLineSize], fat_node.value().cache_line_memory, kPageLineSize);
        }
        return region;
    }
};

}