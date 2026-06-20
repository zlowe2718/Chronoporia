#pragma once
#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <unordered_map>
#include <vector>
#include "fully_persistent_array.h"
#include "globals.h"


// TODO: I want to store the memory snapshots here.  The CoarseSnapshot and LineSnapshots would then only need to save the block base addresses alive at that time
// Structure to look something like the following
//   unordered_map:
//      baseAddress1: [ChangeEvent1, ChangeEvent5]
//      baseAddress2: [ChangeEvent2]
//      baseAddress3: [ChangeEvent3, ChangeEvent4, ChangeEvent6]
// These events will be computed from line snapshots(? Probably a MemoryChangeEvent) and include calls like NtVirtualAlloc, NtProtect, and NtFree
// Each block should include the MemoryBasicInformation and the raw memory at each event.  On restoration or line jump we then do a binary search
// to find the event that's the <= to the current global sequence
// 
// This approach will be expensive memory wise for heap changes since it would copy the entire heap buffer.  If I further tracked by page I might be able to save
// more memory but then the record speed slows down as I need to flag what pages have been changed by running a memcmp per page (or maybe a binary memcmp search)
//
// For memory saving could have a mode that only saves the deltas -> requires iterating every event though to rebuild the final state
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
}