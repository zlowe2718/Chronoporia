#include <catch2/catch_test_macros.hpp>
#include "memory_manager.h"
#include "globals.h"
#include <optional>
#include <vector>

using namespace chronoporia;

// memory_manager.cpp's SnapshotMemory/RestoreMemoryAtSequence walk and rewrite *every* region of
// globals::process_handle's address space, and RestoreMemoryAtSequence starts by freeing every
// private allocation in the target before rebuilding it from history. Running that against our
// own test process (the usual self-process trick used elsewhere in this suite) would free our own
// stack/heap out from under the running test binary. So instead this spins up a real, separate,
// CREATE_SUSPENDED child process to use as the target - it never executes a single instruction
// (the kernel sets up its address space before the thread runs), giving a deterministic snapshot
// target that's safe to mutate and tear down independently of the test runner.
//
// process_memory_history is a module-level static in memory_manager.cpp with no reset hook, so
// this is deliberately the *only* test case here - a second one would see leftover history from
// a since-terminated child process and could act on stale addresses.
namespace {

struct SuspendedChildProcess {
    PROCESS_INFORMATION pi {};
    bool created = false;

    SuspendedChildProcess() {
        STARTUPINFOW si {};
        si.cb = sizeof(si);
        wchar_t cmdline[] = L"C:\\Windows\\System32\\cmd.exe";
        created = CreateProcessW(
            nullptr, cmdline, nullptr, nullptr, FALSE,
            CREATE_SUSPENDED, nullptr, nullptr, &si, &pi
        );
    }

    ~SuspendedChildProcess() {
        if (created) {
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
};

// Finds a committed, private, writable, non-guard page in the target - this exists even before
// the child's first instruction runs (e.g. its TEB and the committed top of its initial stack are
// set up by the kernel at process-creation time).
std::optional<uintptr_t> FindWritablePrivatePage(HANDLE process) {
    MEMORY_BASIC_INFORMATION m {};
    for (char *address = nullptr; VirtualQueryEx(process, address, &m, sizeof(m)) == sizeof(m);
         address = static_cast<char *>(m.BaseAddress) + m.RegionSize)
    {
        if (m.State == MEM_COMMIT && m.Type == MEM_PRIVATE &&
            (m.Protect & PAGE_READWRITE) && !(m.Protect & PAGE_GUARD) &&
            m.RegionSize >= 4096)
        {
            return reinterpret_cast<uintptr_t>(m.BaseAddress);
        }
    }
    return std::nullopt;
}

}

TEST_CASE("SnapshotMemory/RestoreMemoryAtSequence round-trip real memory content for an external process", "[memory_manager]") {
    SuspendedChildProcess child;
    REQUIRE(child.created);
    globals::process_handle = child.pi.hProcess;

    auto target = FindWritablePrivatePage(child.pi.hProcess);
    REQUIRE(target.has_value());
    uintptr_t address = *target;

    BYTE original_byte;
    REQUIRE(ReadProcessMemory(child.pi.hProcess, reinterpret_cast<void *>(address), &original_byte, 1, nullptr));

    SnapshotMemory(/*global_sequence=*/0, /*run_id=*/1, /*run_sequence=*/0);

    BYTE mutated_byte = static_cast<BYTE>(original_byte + 1);
    REQUIRE(WriteProcessMemory(child.pi.hProcess, reinterpret_cast<void *>(address), &mutated_byte, 1, nullptr));

    SnapshotMemory(/*global_sequence=*/1, /*run_id=*/1, /*run_sequence=*/1);

    BYTE confirm_mutated;
    ReadProcessMemory(child.pi.hProcess, reinterpret_cast<void *>(address), &confirm_mutated, 1, nullptr);
    REQUIRE(confirm_mutated == mutated_byte);

    RestoreMemoryAtSequence(/*target_run_id=*/1, /*target_run_sequence=*/0);

    BYTE restored_byte;
    REQUIRE(ReadProcessMemory(child.pi.hProcess, reinterpret_cast<void *>(address), &restored_byte, 1, nullptr));
    REQUIRE(restored_byte == original_byte);

    // ---- Branch + multi-step restore scenario ----
    // Reproduces a (run_id=1, run_seq=0) -> branch -> (run_id=2, run_seq=0) -> (2,1) -> (2,2) ->
    // (2,3) execution history (i.e. RevertToState back to (1,0), then continuing as a new run)
    // over a memory region that grows and has its MBI boundaries reshuffled across snapshots, to
    // exercise restoring to *any* point along that timeline - not just the run's own last
    // snapshot or the branch point itself. own_region is allocated fresh here so its layout is
    // fully under the test's control, independent of whatever the basic round-trip above did to
    // the page found by FindWritablePrivatePage.
    //
    // All of this stays inside ONE TEST_CASE on purpose - see the file-level comment above:
    // process_memory_history has no reset hook, so a second TEST_CASE (a second child process)
    // risks colliding with addresses already recorded here.
    CreateMemoryHistoryBranch(/*target_run_id=*/1, /*target_run_seq=*/0, /*new_run_id=*/2);

    constexpr SIZE_T kPageSize = 4096;
    void *own_region = VirtualAllocEx(child.pi.hProcess, nullptr, 3 * kPageSize, MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(own_region != nullptr);
    uintptr_t own_region_addr = reinterpret_cast<uintptr_t>(own_region);
    uintptr_t page1_addr = own_region_addr + kPageSize;
    uintptr_t page2_addr = own_region_addr + 2 * kPageSize;

    auto fill_page = [&](uintptr_t page_addr, BYTE value) {
        std::vector<BYTE> buf(kPageSize, value);
        REQUIRE(WriteProcessMemory(child.pi.hProcess, reinterpret_cast<void *>(page_addr), buf.data(), kPageSize, nullptr));
    };
    auto read_byte_at = [&](uintptr_t addr) {
        BYTE b = 0;
        REQUIRE(ReadProcessMemory(child.pi.hProcess, reinterpret_cast<void *>(addr), &b, 1, nullptr));
        return b;
    };
    auto query = [&](uintptr_t addr) {
        MEMORY_BASIC_INFORMATION m {};
        REQUIRE(VirtualQueryEx(child.pi.hProcess, reinterpret_cast<void *>(addr), &m, sizeof(m)) == sizeof(m));
        return m;
    };

    // (2,0): page0 committed (PAGE_READWRITE) - one block, one page, content 0xAA.
    REQUIRE(VirtualAllocEx(child.pi.hProcess, own_region, kPageSize, MEM_COMMIT, PAGE_READWRITE) != nullptr);
    fill_page(own_region_addr, 0xAA);
    SnapshotMemory(/*global_sequence=*/2, /*run_id=*/2, /*run_sequence=*/0);

    // (2,1): page1 committed with a *different* protection (PAGE_READONLY) than page0, so
    // VirtualQuery reports it as a separate block at own_region_addr+kPageSize rather than
    // merging it into page0's region. Content 0xBB.
    REQUIRE(VirtualAllocEx(child.pi.hProcess, own_region, 2 * kPageSize, MEM_COMMIT, PAGE_READWRITE) != nullptr);
    fill_page(page1_addr, 0xBB);
    DWORD old_protect;
    REQUIRE(VirtualProtectEx(child.pi.hProcess, reinterpret_cast<void *>(page1_addr), kPageSize, PAGE_READONLY, &old_protect));
    SnapshotMemory(/*global_sequence=*/3, /*run_id=*/2, /*run_sequence=*/1);

    // (2,2): page1's protection flips back to PAGE_READWRITE, so it now merges with page0 into a
    // single two-page block based at own_region_addr - the block_history entry created at (2,1)
    // for own_region_addr+kPageSize is now stale (it will never be visited by SnapshotMemory's
    // scan again, since that address no longer starts its own MBI region). page2 is committed
    // separately with PAGE_READONLY, so it stays a distinct block. page0's content also changes,
    // to make sure restore picks up the merged block's *current* mbi/content rather than the
    // stale one left over from (2,1).
    REQUIRE(VirtualProtectEx(child.pi.hProcess, reinterpret_cast<void *>(page1_addr), kPageSize, PAGE_READWRITE, &old_protect));
    REQUIRE(VirtualAllocEx(child.pi.hProcess, own_region, 3 * kPageSize, MEM_COMMIT, PAGE_READWRITE) != nullptr);
    fill_page(own_region_addr, 0x11);
    fill_page(page2_addr, 0xCC);
    REQUIRE(VirtualProtectEx(child.pi.hProcess, reinterpret_cast<void *>(page2_addr), kPageSize, PAGE_READONLY, &old_protect));
    SnapshotMemory(/*global_sequence=*/4, /*run_id=*/2, /*run_sequence=*/2);

    // (2,3): same layout, page0/page1 get new content again (page2 stays PAGE_READONLY, so it's
    // left as-is). This is the "current" state by the time any RestoreMemoryAtSequence call
    // below runs.
    fill_page(own_region_addr, 0x22);
    fill_page(page1_addr, 0x33);
    SnapshotMemory(/*global_sequence=*/5, /*run_id=*/2, /*run_sequence=*/3);

    // Restoring to the branched run's *first* snapshot must rebuild the pre-growth, single-page
    // state - not leave any of the later-grown pages committed.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/0);
    REQUIRE(query(own_region_addr).RegionSize == kPageSize);
    REQUIRE(read_byte_at(own_region_addr) == 0xAA);

    // Restoring to (2,1) must rebuild exactly the 2-block, pre-merge layout: page0+page1 as
    // separate blocks with separate protections, page2 not committed at all yet.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/1);
    MEMORY_BASIC_INFORMATION page0_at_seq1 = query(own_region_addr);
    REQUIRE(page0_at_seq1.RegionSize == kPageSize);
    REQUIRE(page0_at_seq1.Protect == PAGE_READWRITE);
    REQUIRE(read_byte_at(own_region_addr) == 0xAA);
    MEMORY_BASIC_INFORMATION page1_at_seq1 = query(page1_addr);
    REQUIRE(page1_at_seq1.BaseAddress == reinterpret_cast<void *>(page1_addr));
    REQUIRE(page1_at_seq1.Protect == PAGE_READONLY);
    REQUIRE(read_byte_at(page1_addr) == 0xBB);

    // Restoring to (2,2) must rebuild the *merged* two-page block at own_region_addr with
    // PAGE_READWRITE protection across both pages - the stale block_history entry for
    // own_region_addr+kPageSize (last touched at (2,1), with PAGE_READONLY) must not resurrect
    // and overwrite the second half of the now-merged region.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/2);
    MEMORY_BASIC_INFORMATION merged_at_seq2 = query(own_region_addr);
    REQUIRE(merged_at_seq2.RegionSize == 2 * kPageSize);
    REQUIRE(merged_at_seq2.Protect == PAGE_READWRITE);
    REQUIRE(read_byte_at(own_region_addr) == 0x11);
    REQUIRE(read_byte_at(page1_addr) == 0xBB);
    MEMORY_BASIC_INFORMATION page2_at_seq2 = query(page2_addr);
    REQUIRE(page2_at_seq2.BaseAddress == reinterpret_cast<void *>(page2_addr));
    REQUIRE(page2_at_seq2.Protect == PAGE_READONLY);
    REQUIRE(read_byte_at(page2_addr) == 0xCC);

    // Restoring to the branch point (1,0) predates own_region's creation entirely - the whole
    // allocation must be gone, and the original target byte must still reflect run 1's own
    // history (independent of anything recorded for run 2).
    RestoreMemoryAtSequence(/*target_run_id=*/1, /*target_run_sequence=*/0);
    REQUIRE(query(own_region_addr).State == MEM_FREE);
    REQUIRE(read_byte_at(address) == original_byte);

    // Restoring forward to the latest snapshot and then back to (2,0) must still rebuild the
    // 1-page state - restoring "forward" first is what leaves the larger, later sub-blocks
    // (own_region_addr+kPageSize, +2*kPageSize) actually present/committed in the live process
    // right before the backward restore, which is the scenario most likely to expose a stale
    // sub-block being resurrected instead of skipped.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/3);
    REQUIRE(query(own_region_addr).RegionSize == 2 * kPageSize); // page0+page1, merged (PAGE_READWRITE)
    REQUIRE(query(page2_addr).RegionSize == kPageSize); // page2, separate (PAGE_READONLY)

    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/0);
    REQUIRE(query(own_region_addr).RegionSize == kPageSize);
    REQUIRE(read_byte_at(own_region_addr) == 0xAA);

    // ---- Stress scenario: many merge cycles, like a thread stack growing repeatedly ----
    // A real thread stack grows by committing a new page and moving its guard page down -
    // each move merges the *previous* guard page's block_history entry into the larger region
    // above it, the same single merge event exercised above, but it can happen dozens of times
    // over a real run. Each merge leaves one more stale BlockHistory entry behind (created once,
    // never updated again, never marked retired), so with enough of them the odds that
    // RestoreMemoryAtSequence's unordered_map iteration visits a stale entry *after* the
    // correctly-restored block - clobbering it - go up a lot. A single merge (above) isn't enough
    // to reliably trigger that; this is the same bug with enough stale entries piled up to make
    // it likely to reproduce.
    constexpr int kGrowthSteps = 8;
    constexpr uint32_t kFirstSeq = 4;

    void *own_region2 = VirtualAllocEx(child.pi.hProcess, nullptr, (kGrowthSteps + 2) * kPageSize, MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(own_region2 != nullptr);
    uintptr_t own_region2_addr = reinterpret_cast<uintptr_t>(own_region2);
    auto page_n_addr = [&](int n) { return own_region2_addr + n * kPageSize; };

    REQUIRE(VirtualAllocEx(child.pi.hProcess, own_region2, kPageSize, MEM_COMMIT, PAGE_READWRITE) != nullptr);
    fill_page(own_region2_addr, 0x40);
    SnapshotMemory(/*global_sequence=*/100, /*run_id=*/2, /*run_sequence=*/kFirstSeq);

    for (int step = 1; step <= kGrowthSteps; step++) {
        uintptr_t step_addr = page_n_addr(step);

        // Commit the next page, but as a separate, READONLY block - standing in for a freshly
        // moved guard page, which has different protection than the committed region behind it.
        REQUIRE(VirtualAllocEx(child.pi.hProcess, own_region2, (step + 1) * kPageSize, MEM_COMMIT, PAGE_READWRITE) != nullptr);
        fill_page(step_addr, static_cast<BYTE>(0x40 + step));
        REQUIRE(VirtualProtectEx(child.pi.hProcess, reinterpret_cast<void *>(step_addr), kPageSize, PAGE_READONLY, &old_protect));
        SnapshotMemory(/*global_sequence=*/101 + 2 * (step - 1), /*run_id=*/2, /*run_sequence=*/kFirstSeq + 2 * step - 1);

        // Merge it back into the main region (protection flip, like the guard page being
        // consumed once the stack grows past it) - this is what strands the block_history entry
        // just created above as a stale, never-updated-again ghost.
        REQUIRE(VirtualProtectEx(child.pi.hProcess, reinterpret_cast<void *>(step_addr), kPageSize, PAGE_READWRITE, &old_protect));
        SnapshotMemory(/*global_sequence=*/102 + 2 * (step - 1), /*run_id=*/2, /*run_sequence=*/kFirstSeq + 2 * step);
    }
    uint32_t last_seq = kFirstSeq + 2 * kGrowthSteps;

    // Sanity: restoring to the latest point (no stale ghosts left live in the process to clobber
    // anything) must rebuild the fully-grown, all-merged region correctly.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/last_seq);
    REQUIRE(query(own_region2_addr).RegionSize == (kGrowthSteps + 1) * kPageSize);
    for (int n = 0; n <= kGrowthSteps; n++) {
        REQUIRE(read_byte_at(page_n_addr(n)) == static_cast<BYTE>(0x40 + n));
    }

    // The actual repro: restoring back to the region's *first* snapshot, after every one of the
    // later merge events has left a stale block_history entry behind. Only page0 should end up
    // committed, with its original content - none of the kGrowthSteps stale ghost entries from
    // later in the run should resurrect and extend or corrupt the region.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/kFirstSeq);
    REQUIRE(query(own_region2_addr).RegionSize == kPageSize);
    REQUIRE(read_byte_at(own_region2_addr) == 0x40);

    // ---- Allocation address reuse: a region freed, then a brand new allocation gets handed the
    // exact same allocation_base ----
    // process_memory_history is keyed purely by allocation_base, and the OS is free to hand a
    // freed VA range straight back out to the next VirtualAlloc - which is routine for the
    // process heap, constantly reserving/freeing/re-reserving segments. own_region3 below
    // exercises that: created, actually freed (not just absent from a restore target - actually
    // freed and observed missing by a real SnapshotMemory pass), then a new, content-unrelated
    // allocation is placed at that exact same address and snapshotted again.
    uint32_t reuse_seq_created = last_seq + 1;
    uint32_t reuse_seq_freed = last_seq + 2;
    uint32_t reuse_seq_recreated = last_seq + 3;

    void *own_region3 = VirtualAllocEx(child.pi.hProcess, nullptr, kPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    REQUIRE(own_region3 != nullptr);
    uintptr_t own_region3_addr = reinterpret_cast<uintptr_t>(own_region3);
    fill_page(own_region3_addr, 0x55);
    SnapshotMemory(/*global_sequence=*/200, /*run_id=*/2, /*run_sequence=*/reuse_seq_created);

    REQUIRE(VirtualFreeEx(child.pi.hProcess, own_region3, 0, MEM_RELEASE));
    SnapshotMemory(/*global_sequence=*/201, /*run_id=*/2, /*run_sequence=*/reuse_seq_freed);

    // Request the exact same address back - it was just released above, so this should succeed
    // and land on it, simulating the OS reusing the VA range for an unrelated allocation.
    void *own_region3_reused = VirtualAllocEx(
        child.pi.hProcess, reinterpret_cast<void *>(own_region3_addr), kPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    REQUIRE(own_region3_reused == reinterpret_cast<void *>(own_region3_addr));
    fill_page(own_region3_addr, 0x66);
    SnapshotMemory(/*global_sequence=*/202, /*run_id=*/2, /*run_sequence=*/reuse_seq_recreated);

    // Restoring to the point right after the first allocation must still show its content -
    // the later free+reuse must not retroactively affect this earlier point.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/reuse_seq_created);
    REQUIRE(query(own_region3_addr).State == MEM_COMMIT);
    REQUIRE(read_byte_at(own_region3_addr) == 0x55);

    // Restoring to the point where it's freed must show it as free.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/reuse_seq_freed);
    REQUIRE(query(own_region3_addr).State == MEM_FREE);

    // The actual repro: restoring to the point after the address was reused for the new
    // allocation must rebuild *that* allocation - if the region's CREATED/FREED event lineage
    // isn't refreshed on reuse, this region stays "FREED forever" as far as RestoreMemoryAtSequence
    // is concerned, even though it has valid, more recent history - leaving the address unmapped
    // here instead of holding the new allocation's content.
    RestoreMemoryAtSequence(/*target_run_id=*/2, /*target_run_sequence=*/reuse_seq_recreated);
    REQUIRE(query(own_region3_addr).State == MEM_COMMIT);
    REQUIRE(read_byte_at(own_region3_addr) == 0x66);

    globals::process_handle = 0;
}
