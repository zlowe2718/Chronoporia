#pragma once
#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <unordered_map>
#include <vector>
#include "fully_persistent_array.h"
#include "globals.h"


namespace chronoporia {

enum class MemoryEventType { CREATED, FREED };

struct MBIHistory {
    DWORD state;
    DWORD protect;
    DWORD type;
    uint64_t size;
};

template <uint64_t PageSize>
struct BlockHistory {
    uintptr_t allocation_base;
    uintptr_t base_address;

    // A sub-block's address range can be absorbed into a different block when its MBI boundary
    // shifts (e.g. a thread stack's guard page moving down on growth merges the old guard page's
    // block into the committed region above it). mbi_history has no way to express "this address
    // range stopped being this block as of here" - it just keeps returning the last value it was
    // ever given - so without this, a stale BlockHistory can be resurrected by
    // RestoreMemoryAtSequence over the very block its address range was absorbed into. This
    // mirrors MemoryRegionHistory::events (CREATED/FREED) but at the sub-block granularity.
    FullyPersistentArray<MemoryEventType> events;
    FullyPersistentArray<MBIHistory> mbi_history;
    // An unordered_map of page address to its fully persistent memory
    std::unordered_map<uintptr_t, FullyPersistentCacheArray<PageSize>> page_history;
};

// This is a fully persistent memory region
template <uint64_t PageSize>
struct MemoryRegionHistory {
    uint64_t total_region_size;
    DWORD allocation_protect;

    FullyPersistentArray<MemoryEventType> events;
    // An unordered map of base address ptr to the fully persistent block history
    std::unordered_map<uintptr_t, BlockHistory<PageSize>> block_history;
};

void SnapshotMemory(uint64_t global_sequence, uint32_t run_id, uint32_t run_sequence);
void RestoreMemoryAtSequence(uint32_t target_run_id, uint32_t target_run_sequence);
void AddBlacklistAddress(uintptr_t address);
void CreateMemoryHistoryBranch(uint32_t target_run_id, uint32_t target_run_seq, uint32_t new_run_id);
}