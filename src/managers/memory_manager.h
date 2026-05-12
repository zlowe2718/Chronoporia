#pragma once
#define WIN32_LEAN_AND_MEAN
#include <cstddef>
#include <Windows.h>
#include <unordered_map>
#include <vector>
#include "partially_persistent_arrays.h"
#include "globals.h"

#ifdef MEMORY_DEBUG
    inline constexpr bool kMemoryDebug = true;
#else
    inline constexpr bool kMemoryDebug = false;
#endif

namespace chronoporia {

enum class MemoryEventType { CREATED, FREED };

struct MBIHistory {
    uint64_t global_sequence;
    DWORD state;
    DWORD protect;
    DWORD type;
    uint64_t size;
};

template <uint64_t PageSize>
struct BlockHistory {
    uintptr_t allocation_base;
    uintptr_t base_address;

    std::vector<MBIHistory> mbi_history;
    // An unordered_map of page address to its partially persistent memory
    std::unordered_map<uintptr_t, PartiallyPersistentCacheArray<PageSize>> page_history;
};

struct MemoryEvent {
    uint64_t global_sequence;
    MemoryEventType event_type;
};

// This is a partially persistent memory region
template <uint64_t PageSize>
struct MemoryRegionHistory {
    uint64_t total_region_size;
    DWORD allocation_protect;

    std::vector<MemoryEvent> events;
    // An unordered map of base address ptr to the partially persistent block history
    std::unordered_map<uintptr_t, BlockHistory<PageSize>> block_history;
};

void CoarseSnapshot(uint64_t global_sequence);
void RestoreMemoryAtSequence(uint64_t global_sequence);

template <bool MemoryDebug>
inline void VerifyMemory(void *src_1, void *src_2, size_t size) {
    if constexpr (MemoryDebug) {
        if(memcmp(src_1, src_2, size) != 0) {
            __debugbreak();
        }
    }
}

template <bool MemoryDebug>
inline void VerifyMemoryAtProcessAddress(uintptr_t target_address, void *src_2, size_t size) {
    if constexpr (MemoryDebug) {
        std::vector<unsigned char> buffer;
        buffer.resize(size);
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(target_address), buffer.data(), size, 0);
        if(memcmp(buffer.data(), src_2, size) != 0) {
            __debugbreak();
        }
    }
}
}