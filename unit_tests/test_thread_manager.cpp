#include <catch2/catch_test_macros.hpp>
#include "thread_manager.h"
#include "thread_utils.h"
#include "globals.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace chronoporia;
using namespace std::chrono_literals;

namespace {

struct SpinningWorker {
    std::atomic<bool> stop {false};
    std::atomic<uint64_t> counter {0};
    std::thread thread;
    DWORD thread_id {0};

    SpinningWorker() {
        thread = std::thread([this] {
            while (!stop.load(std::memory_order_relaxed)) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
        thread_id = GetThreadId(thread.native_handle());
    }

    // Caller is responsible for making sure the thread isn't left suspended (it would never see
    // `stop` and join() would hang forever).
    void StopAndJoin() {
        stop.store(true, std::memory_order_relaxed);
        if (thread.joinable()) thread.join();
    }
};

}

// Like module_manager, process_thread_history/current_thread_ids are module-level statics with
// no reset hook, and RestoreThreadsAtSequence loops over *every* tracked thread calling
// ExecutionTree::GetState (exact (run_id, run_seq) match, asserts/aborts if missing) on each.
// So this is one TEST_CASE driving a single coherent timeline rather than several independent
// ones - that both keeps every tree's nodes consistent at each queried tick and lets one
// RestoreThreadsAtSequence call exercise both outcomes (context replace vs. termination) at once.
TEST_CASE("RestoreThreadsAtSequence replaces a still-present thread's context and terminates an absent one", "[thread_manager]") {
    constexpr uint32_t run_id = 11;
    SpinningWorker survivor;
    SpinningWorker doomed;

    GetThreadHandle(survivor.thread_id);
    GetThreadHandle(doomed.thread_id);
    SuspendThreadId(survivor.thread_id);
    SuspendThreadId(doomed.thread_id);

    // tick 0: both present
    TrackThread(survivor.thread_id, 0, run_id, 0);
    TrackThread(doomed.thread_id, 0, run_id, 0);

    // tick 1: doomed leaves the timeline; survivor is carried forward by SnapshotThreads (which
    // only iterates currently-tracked thread ids, i.e. whatever's left after the untrack above)
    UntrackThread(doomed.thread_id, 1, run_id, 1);
    SnapshotThreads(1, run_id, 1);

    // Move from tick 0 (both present) to tick 1 (only survivor present): survivor should have its
    // context replaced and stay alive; doomed has no matching node at the target and should be
    // terminated.
    RestoreThreadsAtSequence(/*from_run_id=*/run_id, /*from_run_seq=*/0, /*to_run_id=*/run_id, /*to_run_seq=*/1);

    // TerminateThread only *requests* termination - wait for it to actually complete before
    // checking the exit code, otherwise this races and can observe STILL_ACTIVE.
    REQUIRE(WaitForSingleObject(doomed.thread.native_handle(), 1000) == WAIT_OBJECT_0);

    DWORD doomed_exit_code = STILL_ACTIVE;
    GetExitCodeThread(doomed.thread.native_handle(), &doomed_exit_code);
    REQUIRE(doomed_exit_code != STILL_ACTIVE);

    doomed.thread.join(); // the OS thread is already dead; this just reclaims the std::thread

    ResumeThreadId(survivor.thread_id);

    survivor.StopAndJoin();
}

TEST_CASE("TrackAllProgramThreads/SnapshotThreads run against the real current process without crashing", "[thread_manager]") {
    globals::process_id = GetCurrentProcessId();

    REQUIRE_NOTHROW(TrackAllProgramThreads(0, 12, 0));
    REQUIRE_NOTHROW(SnapshotThreads(1, 12, 1));
}
