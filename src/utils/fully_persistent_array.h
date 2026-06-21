#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

namespace chronoporia {

using CacheLine = std::array<std::byte, 64>;
using PageMemory = std::array<std::byte, 4096>;

// Fully persistent counterpart to PartiallyPersistentCacheArray: a new run can branch off ANY
// prior (run_id, run_seq) - e.g. after an ExecutionTree::RevertToState - not just the most
// recently active run. Because a revert can jump back further than the last branch, a run's
// lineage can't be inferred from update arrival order alone, so each branch is registered
// explicitly via register_branch.
//
// run 1: A -> B -> C
//          \-> run 2 (branched from A): D -> E
// run 3 (branched from B): F -> G
//
// run_id + run_seq is the unique identifier for a FatNode and what lookups are keyed on.
// global_seq is just carried along on each FatNode for ordering elsewhere; it is not used here.
//
// Updates stay an O(1) append per touched cache line; get() walks the lineage chain to
// reconstruct a page, which is fine since reads are allowed to be slower than writes here.
template <uint64_t PageSize> // Page size in bytes
class FullyPersistentCacheArray {
public:
    static constexpr uint16_t kCacheLineSize = 64;
    static_assert(PageSize % kCacheLineSize == 0, "Template parameter PageSize must be 64 byte aligned.");
    static constexpr uint32_t CacheLineAmount = PageSize / kCacheLineSize;

    struct FatNode {
        uint32_t run_id;
        uint32_t run_seq;
        uint64_t global_seq;
        std::array<std::byte, kCacheLineSize> cache_memory;
    };

    // Where a run forked from: its parent run, and the run_seq within that parent it branched at.
    struct BranchPoint {
        uint32_t parent_run_id;
        uint32_t branch_run_seq;
    };

    struct RunHistory {
        // sorted by run_seq, since a single run's own updates always arrive in increasing order
        std::array<std::vector<FatNode>, CacheLineAmount> cache_lines;
    };

    uint32_t created_run_id;
    uint32_t created_run_seq;
    std::unordered_map<uint32_t, RunHistory> runs; // run_id -> per-cache-line delta history for that run
    std::unordered_map<uint32_t, BranchPoint> lineage; // run_id -> where it branched from (root run has no entry)

    // Mirror of the most recently written contents of this page, kept in sync by the
    // constructor and update(). Recording always advances forward along a single active
    // run, so this always reflects that run's current state - it lets callers on the hot
    // record path (e.g. SnapshotMemory's diff-before-write check) read the current page
    // in O(1) instead of reconstructing it via find_fat_node() on every cache line.
    // Historical/branched reads still go through get(), which is unaffected by this cache.
    PageMemory latest_page_memory {};

    FullyPersistentCacheArray() {};

    FullyPersistentCacheArray(uint32_t run_id, uint32_t run_seq, uint64_t global_seq, const std::array<CacheLine, CacheLineAmount>& page_memory) {
        auto& history = runs[run_id];

        for (uint32_t cache_line_idx = 0; cache_line_idx < CacheLineAmount; cache_line_idx++) {
            FatNode fat_node {};
            fat_node.run_id = run_id;
            fat_node.run_seq = run_seq;
            fat_node.global_seq = global_seq;
            memcpy(fat_node.cache_memory.data(), page_memory[cache_line_idx].data(), kCacheLineSize);

            history.cache_lines[cache_line_idx].push_back(std::move(fat_node));

            memcpy(latest_page_memory.data() + cache_line_idx * kCacheLineSize, page_memory[cache_line_idx].data(), kCacheLineSize);
        }

        created_run_id = run_id;
        created_run_seq = run_seq;
    };

    // Call once when run_id first branches off parent_run_id, at the point parent_run_id had reached branch_run_seq.
    void register_branch(uint32_t run_id, uint32_t parent_run_id, uint32_t branch_run_seq) {
        lineage[run_id] = BranchPoint { parent_run_id, branch_run_seq };
    }

    // Fast path: caller already knows which single cache line changed.
    void update(uint32_t run_id, uint32_t run_seq, uint64_t global_seq, uint32_t cache_line_idx, const CacheLine& cache_line_memory) {
        FatNode fat_node {};
        fat_node.run_id = run_id;
        fat_node.run_seq = run_seq;
        fat_node.global_seq = global_seq;
        memcpy(fat_node.cache_memory.data(), cache_line_memory.data(), kCacheLineSize);

        runs[run_id].cache_lines[cache_line_idx].push_back(std::move(fat_node));

        memcpy(latest_page_memory.data() + cache_line_idx * kCacheLineSize, cache_line_memory.data(), kCacheLineSize);
    }

    // Convenience path: diff two full pages and record only the cache lines that changed.
    void update(
        uint32_t run_id,
        uint32_t run_seq,
        uint64_t global_seq,
        const std::array<CacheLine, CacheLineAmount>& page_memory,
        const std::array<CacheLine, CacheLineAmount>& previous_page_memory
    ) {
        for (uint32_t cache_line_idx = 0; cache_line_idx < CacheLineAmount; cache_line_idx++) {
            if (memcmp(page_memory[cache_line_idx].data(), previous_page_memory[cache_line_idx].data(), kCacheLineSize) == 0) continue;

            update(run_id, run_seq, global_seq, cache_line_idx, page_memory[cache_line_idx]);
        }
    };

    // TODO: caller should ensure (run_id, run_seq) actually exists in the run tree
    PageMemory get(uint32_t run_id, uint32_t run_seq) const {
        PageMemory rebuilt_page {};

        for (uint32_t cache_line_idx = 0; cache_line_idx < CacheLineAmount; cache_line_idx++) {
            const FatNode* fat_node = find_fat_node(cache_line_idx, run_id, run_seq);
            if (fat_node == nullptr) continue;

            memcpy(
                rebuilt_page.data() + cache_line_idx * kCacheLineSize,
                fat_node->cache_memory.data(),
                kCacheLineSize
            );
        }

        return rebuilt_page;
    };

    // O(1) read of the most recently written page contents, for the active recording run.
    // See latest_page_memory for why this is safe to use in place of get() on the hot path.
    const PageMemory& latest() const {
        return latest_page_memory;
    }

private:
    // Walk the branch lineage from (run_id, run_seq) back towards the root, returning the
    // most recent delta visible from that point in history for this cache line.
    const FatNode* find_fat_node(uint32_t cache_line_idx, uint32_t run_id, uint32_t run_seq) const {
        uint32_t search_run_id = run_id;
        uint32_t search_upper_bound = run_seq;

        while (true) {
            auto run_it = runs.find(search_run_id);
            if (run_it != runs.end()) {
                const auto& history = run_it->second.cache_lines[cache_line_idx];

                // binary search to find the first entry with run_seq > target, then step back one
                auto idx = std::upper_bound(
                    history.begin(), history.end(), search_upper_bound,
                    [](uint32_t target, const FatNode& node) { return target < node.run_seq; }
                ) - history.begin();

                if (idx > 0) return &history[idx - 1];
            }

            auto branch_it = lineage.find(search_run_id);
            if (branch_it == lineage.end()) return nullptr; // reached the root with nothing found

            search_run_id = branch_it->second.parent_run_id;
            search_upper_bound = branch_it->second.branch_run_seq;
        }
    }
};

// Generic counterpart to FullyPersistentCacheArray: same run_id/run_seq keyed history and
// branch lineage walk, but for a single arbitrary Data value per update instead of a page's
// worth of cache lines. Used for state that changes in infrequent, whole-value jumps (e.g.
// a memory block's commit/protect state, or an allocation's created/freed lifecycle event)
// rather than per-cache-line bytes.
template <typename Data>
class FullyPersistentArray {
public:
    struct FatNode {
        uint32_t run_id;
        uint32_t run_seq;
        uint64_t global_seq;
        Data data;
    };

    // Where a run forked from: its parent run, and the run_seq within that parent it branched at.
    struct BranchPoint {
        uint32_t parent_run_id;
        uint32_t branch_run_seq;
    };

    uint32_t created_run_id;
    uint32_t created_run_seq;
    std::unordered_map<uint32_t, std::vector<FatNode>> runs; // run_id -> history (sorted by run_seq) for that run
    std::unordered_map<uint32_t, BranchPoint> lineage; // run_id -> where it branched from (root run has no entry)

    FullyPersistentArray() {};

    FullyPersistentArray(uint32_t run_id, uint32_t run_seq, uint64_t global_seq, const Data& data) {
        runs[run_id].push_back(FatNode {run_id, run_seq, global_seq, data});
        created_run_id = run_id;
        created_run_seq = run_seq;
    };

    // Call once when run_id first branches off parent_run_id, at the point parent_run_id had reached branch_run_seq.
    void register_branch(uint32_t run_id, uint32_t parent_run_id, uint32_t branch_run_seq) {
        lineage[run_id] = BranchPoint { parent_run_id, branch_run_seq };
    }

    void update(uint32_t run_id, uint32_t run_seq, uint64_t global_seq, const Data& data) {
        runs[run_id].push_back(FatNode {run_id, run_seq, global_seq, data});
    }

    // TODO: caller should ensure (run_id, run_seq) actually exists in the run tree
    Data get(uint32_t run_id, uint32_t run_seq) const {
        return find_fat_node(run_id, run_seq)->data;
    }

    // Same lookup as get(), but returns std::nullopt instead of crashing when the tracked entity
    // (region/block/etc.) wasn't created yet as of (run_id, run_seq) - e.g. it branched off a
    // lineage point that predates the entity's creation.
    std::optional<Data> try_get(uint32_t run_id, uint32_t run_seq) const {
        const FatNode* node = find_fat_node(run_id, run_seq);
        if (!node) return std::nullopt;
        return node->data;
    }

private:
    // Walk the branch lineage from (run_id, run_seq) back towards the root, returning the
    // most recent delta visible from that point in history.
    const FatNode* find_fat_node(uint32_t run_id, uint32_t run_seq) const {
        uint32_t search_run_id = run_id;
        uint32_t search_upper_bound = run_seq;

        while (true) {
            auto run_it = runs.find(search_run_id);
            if (run_it != runs.end()) {
                const auto& history = run_it->second;

                auto idx = std::upper_bound(
                    history.begin(), history.end(), search_upper_bound,
                    [](uint32_t target, const FatNode& node) { return target < node.run_seq; }
                ) - history.begin();

                if (idx > 0) return &history[idx - 1];
            }

            auto branch_it = lineage.find(search_run_id);
            if (branch_it == lineage.end()) return nullptr; // reached the root with nothing found

            search_run_id = branch_it->second.parent_run_id;
            search_upper_bound = branch_it->second.branch_run_seq;
        }
    }
};

}
