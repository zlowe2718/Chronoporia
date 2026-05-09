#pragma once
#include <array>
#include <vector>

namespace chronoporia {

using CacheLine = std::array<std::byte, 64>;
using PageMemory = std::array<std::byte, 4096>;
// TODO: Get page size from GetSystemInfo

// Typically a FatNode implementation of a partially persistent array is a vector of structs of changed value and version
// Since this is for memory I'm changing the implementation a little to be more cache aligned
// This means the "FatNode" is just a cache line of 64 bytes and to be cache efficient I', holding the version number in a parrallel array
// We're going to have a pp_array per operating page size hence the hard set array sizes at the Page Size in bytes
//
// Usage: PartiallyPersistentArray<4096>
template <uint64_t PageSize> // Page size in bytes
class PartiallyPersistentCacheArray {
public:
    static constexpr uint16_t kCacheLineSize = 64;
    static_assert(PageSize % kCacheLineSize == 0, "Template parameter PageSize must be 64 byte aligned.");
    static constexpr uint32_t CacheLineAmount = PageSize / kCacheLineSize;

    struct FatNode {
        std::array<std::byte, kCacheLineSize> cache_memory; 
    };

    uint64_t created_version;
    std::array<std::vector<uint64_t>, CacheLineAmount> versions;
    std::array<std::vector<FatNode>, CacheLineAmount> fat_nodes; // Each cache line in a page will have its own "history" or FatNode

    PartiallyPersistentCacheArray() {};

    // TODO: can I change this to support move semantics instead of memcpy
    PartiallyPersistentCacheArray(uint64_t global_sequence, const std::array<CacheLine, CacheLineAmount>& page_memory) {
        for (uint32_t cache_line_idx = 0; cache_line_idx < CacheLineAmount; cache_line_idx++) {
            const auto& current_cache_line = page_memory[cache_line_idx];

            FatNode fat_node {};
            memcpy(fat_node.cache_memory.data(), current_cache_line.data(), kCacheLineSize);

            versions[cache_line_idx].push_back(global_sequence);
            fat_nodes[cache_line_idx].push_back(std::move(fat_node));
        }
        created_version = global_sequence;
    };

    // TODO: I don't think I need previous_page_memory, I can just memcmp with fat_nodes[cache_line_idx].back()
    //  i.e. the most recent fat node change for the cache line
    void update(
        uint64_t global_sequence, 
        const std::array<CacheLine, CacheLineAmount>& page_memory,
        const std::array<CacheLine, CacheLineAmount>& previous_page_memory
    ) {
        for (uint32_t cache_line_idx = 0; cache_line_idx < CacheLineAmount; cache_line_idx++) {
            const auto& current_cache_line = page_memory[cache_line_idx];
            const auto& previous_cache_line = page_memory[cache_line_idx];

            if (memcmp(current_cache_line.data(), previous_cache_line.data(), kCacheLineSize) == 0) continue;

            FatNode fat_node {};
            memcpy(fat_node.cache_memory.data(), current_cache_line.data(), kCacheLineSize);

            versions[cache_line_idx].push_back(global_sequence);
            fat_nodes[cache_line_idx].push_back(std::move(fat_node));
        }

    };

    // TODO: caller should ensure that target sequence is never less than the created version
    PageMemory get(uint64_t target_sequence) const {
        PageMemory rebuilt_page {};

        // Find the latest version at or before the target sequence
        for (uint32_t cache_line_idx = 0; cache_line_idx < CacheLineAmount; cache_line_idx++) {
            const auto& fat_node_versions = versions[cache_line_idx];
            
            // binary search to find the first entry with version > target_sequence, then step back one
            auto it = std::upper_bound(fat_node_versions.begin(), fat_node_versions.end(), target_sequence) - fat_node_versions.begin();

            memcpy(
                rebuilt_page.data() + cache_line_idx * kCacheLineSize, 
                fat_nodes[cache_line_idx][it - 1].cache_memory.data(),
                kCacheLineSize
            );
        } 

        return rebuilt_page;
    };

};

template <typename Data>
class PartiallyPersistentArray {
public:

    struct FatNode {
        uint64_t global_seq;
        Data data; 
    };

    uint64_t created_version;
    std::vector<FatNode> fat_nodes;

    PartiallyPersistentArray() {};

    // TODO: can I change this to support move semantics instead of memcpy
    PartiallyPersistentArray(uint64_t global_sequence, const Data& data) {
        FatNode fat_node {global_sequence, data};
        fat_nodes.push_back(std::move(fat_node));
        created_version = global_sequence;
    };

    void update(
        uint64_t global_sequence, 
        const Data& data
    ) {
        FatNode fat_node {global_sequence, data};
        fat_nodes.push_back(std::move(fat_node));
    };

    // TODO: caller should ensure that target sequence is never less than the created version
    Data get(uint64_t target_sequence) const {
        // binary search to find the first entry with version > target_sequence, then step back one
        auto idx = std::upper_bound(fat_nodes.begin(), fat_nodes.end(), target_sequence,
            [](uint64_t seq, const FatNode& fat_node) {
                return seq < fat_node.global_seq;
            }
        ) - fat_nodes.begin();

        
        return fat_nodes[idx - 1].data;
    };

};

}