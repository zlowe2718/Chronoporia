#include <catch2/catch_test_macros.hpp>
#include "snapshot_log.h"
#include "globals.h"

using namespace chronoporia;

namespace {

// snapshot_history is a module-level static in snapshot_log.cpp with no accessor, so the only
// thing observable from outside the translation unit is StartSnapshotHistory's effect on
// globals (run_id/run_sequence/global_sequence at construction time). SnapshotProcess is
// deliberately NOT exercised here - see the bottom of this file for why.
struct SnapshotFixture {
    SnapshotFixture() {
        globals::process_id = GetCurrentProcessId();
        globals::process_handle = GetCurrentProcess();
    }
    ~SnapshotFixture() {
        globals::process_handle = 0;
    }
};

}

TEST_CASE_METHOD(SnapshotFixture, "StartSnapshotHistory resets the snapshot history without throwing", "[snapshot_log]") {
    REQUIRE_NOTHROW(StartSnapshotHistory());
}

// SnapshotProcess is intentionally NOT unit tested here.
//
// It unconditionally calls SnapshotMemory(global_sequence, run_id, run_sequence), which walks
// VirtualQueryEx over the ENTIRE virtual address space of globals::process_handle and, for every
// committed page in every region, diffs it against history and copies it into a
// FullyPersistentCacheArray<4096>. There is no parameter or seam to scope that down.
//
// Pointed at the unit test binary's own process (the only process available in-process, with no
// mocking layer for VirtualQueryEx/ReadProcessMemory), a single call took well over a minute in
// a debug build and never reliably finished within a sane test timeout - i.e. it is not a fast,
// deterministic unit test, it is closer to a slow integration benchmark.
//
// To make this testable, one of the following would be needed:
//   1. Split the bookkeeping (run_sequence increment, AddChild into snapshot_history) out of
//      SnapshotProcess from the actual I/O (SnapshotThreads/SnapshotMemory), so the bookkeeping
//      can be tested in isolation - e.g. take the snapshot functions as injectable parameters
//      or function pointers.
//   2. Run SnapshotMemory against a deliberately small, attached child/dummy process instead of
//      the test binary itself, and accept this as a slow integration test (run separately from
//      the fast unit test suite, with a generous timeout).
//   3. Add a way to scope SnapshotMemory to an explicit address range/allocation list (something
//      AddBlacklistAddress hints at, but inverted - an explicit include-list) so tests can target
//      a handful of pages instead of the whole address space.
