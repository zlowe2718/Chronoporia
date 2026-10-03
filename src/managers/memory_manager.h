#pragma once
#include <cstdint>

namespace chronoporia {
    // Snapshot the progran's memory
    void SnapshotMemory(uint64_t global_sequence, uint32_t run_id, uint32_t run_sequence);
    
    // Restore the program's memory at a specified snapshot sequence
    void RestoreMemoryAtSequence(uint32_t target_run_id, uint32_t target_run_sequence);
    
    // Add an address to the blacklist so we don't track or or restore at this address 
    // This should be used for any chronoporia managed addresses in the process like trampolines and dlls 
    void AddBlacklistAddress(uintptr_t address);

    // Make a branch for all the memory tracking.  Needs to be called when reverting to another state for
    // full persistence to be tracked properly
    void CreateMemoryHistoryBranch(uint32_t target_run_id, uint32_t target_run_seq, uint32_t new_run_id);
}