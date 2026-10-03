#include <catch2/catch_test_macros.hpp>
#include "thread_utils.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace chronoporia;
using namespace std::chrono_literals;

namespace {

// A worker that spins incrementing a counter so tests can observe whether the OS thread is
// actually making progress (vs. just checking that an API call didn't throw).
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

    ~SpinningWorker() {
        stop.store(true, std::memory_order_relaxed);
        CloseHandleForThread(thread_id, GetThreadHandle(thread_id));
        if (thread.joinable()) thread.join();
    }
};

}

TEST_CASE("GetThreadHandle opens and caches a handle for a live thread id", "[thread_utils]") {
    SpinningWorker worker;

    HANDLE handle = GetThreadHandle(worker.thread_id);
    REQUIRE(handle != nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);

    // second call should hit the cache and return the same handle value
    HANDLE handle_again = GetThreadHandle(worker.thread_id);
    REQUIRE(handle_again == handle);
}

TEST_CASE("CloseHandleForThread evicts the cache so a later lookup reopens a fresh handle", "[thread_utils]") {
    SpinningWorker worker;

    HANDLE handle = GetThreadHandle(worker.thread_id);
    CloseHandleForThread(worker.thread_id, handle);

    HANDLE reopened = GetThreadHandle(worker.thread_id);
    REQUIRE(reopened != nullptr);

    CloseHandleForThread(worker.thread_id, reopened);
}

TEST_CASE("GetRipAddress returns a non-zero instruction pointer for a running thread", "[thread_utils]") {
    SpinningWorker worker;

    uint64_t rip = GetRipAddress(worker.thread_id);
    REQUIRE(rip != 0);

    CloseHandleForThread(worker.thread_id, GetThreadHandle(worker.thread_id));
}

TEST_CASE("SuspendThreadId halts progress and ResumeThreadId restores it", "[thread_utils]") {
    SpinningWorker worker;

    // SuspendThreadId/ResumeThreadId look up an already-cached handle rather than opening one
    // themselves, so the thread must be registered via GetThreadHandle first.
    GetThreadHandle(worker.thread_id);

    // make sure the thread is actually running first
    std::this_thread::sleep_for(20ms);
    REQUIRE(worker.counter.load() > 0);

    SuspendThreadId(worker.thread_id);
    uint64_t counter_at_suspend = worker.counter.load();
    std::this_thread::sleep_for(50ms);
    REQUIRE(worker.counter.load() == counter_at_suspend);

    ResumeThreadId(worker.thread_id);
    std::this_thread::sleep_for(20ms);
    REQUIRE(worker.counter.load() > counter_at_suspend);

    CloseHandleForThread(worker.thread_id, GetThreadHandle(worker.thread_id));
}

TEST_CASE("SuspendAllThreads/ResumeAllThreads affect every previously-opened thread handle", "[thread_utils]") {
    SpinningWorker worker_a;
    SpinningWorker worker_b;

    // GetThreadHandle must be called at least once so the thread is tracked by these globals.
    GetThreadHandle(worker_a.thread_id);
    GetThreadHandle(worker_b.thread_id);

    std::this_thread::sleep_for(20ms);
    SuspendAllThreads();

    uint64_t a_at_suspend = worker_a.counter.load();
    uint64_t b_at_suspend = worker_b.counter.load();
    std::this_thread::sleep_for(50ms);
    REQUIRE(worker_a.counter.load() == a_at_suspend);
    REQUIRE(worker_b.counter.load() == b_at_suspend);

    ResumeAllThreads();
    std::this_thread::sleep_for(20ms);
    REQUIRE(worker_a.counter.load() > a_at_suspend);
    REQUIRE(worker_b.counter.load() > b_at_suspend);
}

TEST_CASE("ReplaceThreadContext writes back a register value while the thread is suspended", "[thread_utils]") {
    SpinningWorker worker;
    GetThreadHandle(worker.thread_id);

    SuspendThreadId(worker.thread_id);

    CONTEXT ctx = GetThreadContextFromId(worker.thread_id);
    ctx.R10 = 0x1122334455667788ULL;
    ReplaceThreadContext(worker.thread_id, ctx);

    CONTEXT ctx_after = GetThreadContextFromId(worker.thread_id);
    REQUIRE(ctx_after.R10 == 0x1122334455667788ULL);

    ResumeThreadId(worker.thread_id);
}
