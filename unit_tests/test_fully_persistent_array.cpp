#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include "fully_persistent_array.h"
#include <cstring>
#include <string>

using chronoporia::FullyPersistentArray;
using chronoporia::FullyPersistentCacheArray;
using chronoporia::CacheLine;
using chronoporia::PageMemory;

namespace {

CacheLine MakeCacheLine(std::byte fill) {
    CacheLine line {};
    line.fill(fill);
    return line;
}

}

TEST_CASE("FullyPersistentArray: construction is readable at its own (run, seq)", "[fully_persistent_array]") {
    FullyPersistentArray<int> arr(/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 42);
    REQUIRE(arr.get(1, 0) == 42);
}

TEST_CASE("FullyPersistentArray: update within a run is visible at and after its run_seq", "[fully_persistent_array]") {
    FullyPersistentArray<int> arr(1, 0, 0, 1);
    arr.update(1, 1, 1, 2);
    arr.update(1, 2, 2, 3);

    REQUIRE(arr.get(1, 0) == 1);
    REQUIRE(arr.get(1, 1) == 2);
    REQUIRE(arr.get(1, 2) == 3);
}

TEST_CASE("FullyPersistentArray: querying a run_seq between updates returns the most recent prior value", "[fully_persistent_array]") {
    FullyPersistentArray<int> arr(1, 0, 0, 1);
    arr.update(1, 5, 1, 99);

    // run_seq 3 was never written directly, so it should see the value as of run_seq 0
    REQUIRE(arr.get(1, 3) == 1);
    REQUIRE(arr.get(1, 5) == 99);
}

TEST_CASE("FullyPersistentArray: a branched run sees the parent's history up to the branch point", "[fully_persistent_array]") {
    FullyPersistentArray<int> arr(1, 0, 0, 100);
    arr.update(1, 1, 1, 200);
    arr.update(1, 2, 2, 300);

    // run 2 branched off run 1 at run_seq 1, i.e. it should see 200, not 300
    arr.register_branch(/*run_id=*/2, /*parent_run_id=*/1, /*branch_run_seq=*/1);
    arr.update(2, 0, 3, 999);

    REQUIRE(arr.get(2, 0) == 999);
    // run 2 itself has no entry prior to run_seq 0 other than the branch, so a lookup before
    // its own first update should fall through to the parent at the branch point.
    REQUIRE(arr.get(1, 1) == 200);
}

TEST_CASE("FullyPersistentArray: lineage walk supports multiple branch hops", "[fully_persistent_array]") {
    FullyPersistentArray<int> arr(1, 0, 0, 1);
    arr.update(1, 1, 1, 2);

    arr.register_branch(2, 1, 1);
    arr.update(2, 5, 2, 20);

    arr.register_branch(3, 2, 5);
    arr.update(3, 10, 3, 30);

    // run 3 has its own update at run_seq 10
    REQUIRE(arr.get(3, 10) == 30);
    // querying run 3 before its own first update should walk: run3 -> run2 @5 -> run1 @1
    REQUIRE(arr.get(3, 0) == 20);
}

TEST_CASE("FullyPersistentArray: works with non-trivial Data via copy and move construction", "[fully_persistent_array]") {
    std::string initial = "root";
    FullyPersistentArray<std::string> arr(1, 0, 0, initial);
    arr.update(1, 1, 1, std::string("child"));

    REQUIRE(arr.get(1, 0) == "root");
    REQUIRE(arr.get(1, 1) == "child");
}

// Mirrors the two cases process_memory_history relies on in memory_manager.cpp's
// RestoreMemoryAtSequence: try_get() must return std::nullopt rather than stale/wrong data
// when a run's lineage simply never reaches the entity's creation point.
TEST_CASE("FullyPersistentArray: try_get returns nullopt when queried from a lineage that predates creation", "[fully_persistent_array]") {
    // Entity (e.g. a region's CREATED/FREED event history) is created under run 1 at run_seq 5.
    FullyPersistentArray<int> events(/*run_id=*/1, /*run_seq=*/5, /*global_seq=*/0, /*data=*/100 /*CREATED*/);

    // run 2 branches off run 1 at run_seq 2, i.e. before the entity existed in that lineage.
    events.register_branch(/*run_id=*/2, /*parent_run_id=*/1, /*branch_run_seq=*/2);

    REQUIRE(events.try_get(2, 0) == std::nullopt);
    REQUIRE(events.try_get(2, 100) == std::nullopt);

    // Sanity check: the parent run itself sees the entity once past its creation point.
    REQUIRE(events.try_get(1, 5) == 100);
}

TEST_CASE("FullyPersistentArray: try_get returns nullopt for a freed entity queried from an unrelated branch", "[fully_persistent_array]") {
    // Entity is created then freed entirely within run 1's own history.
    FullyPersistentArray<int> events(/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, /*data=*/100 /*CREATED*/);
    events.update(/*run_id=*/1, /*run_seq=*/3, /*global_seq=*/1, /*data=*/200 /*FREED*/);

    // run 5 has no recorded history and was never registered as branching off run 1 (or anything
    // else) - it is completely unrelated to the lineage that created and freed the entity.
    REQUIRE(events.try_get(5, 10) == std::nullopt);

    // Sanity check: run 1 itself still sees the FREED state in its own history.
    REQUIRE(events.try_get(1, 10) == 200);
}

TEST_CASE("FullyPersistentCacheArray: construction stores every cache line of the page", "[fully_persistent_cache_array]") {
    constexpr uint64_t kPageSize = 128; // 2 cache lines
    using Array = FullyPersistentCacheArray<kPageSize>;

    std::array<CacheLine, Array::CacheLineAmount> page {
        MakeCacheLine(std::byte{0xAA}),
        MakeCacheLine(std::byte{0xBB}),
    };

    Array arr(1, 0, 0, page);
    PageMemory rebuilt = arr.get(1, 0);

    REQUIRE(std::memcmp(rebuilt.data(), page[0].data(), Array::kCacheLineSize) == 0);
    REQUIRE(std::memcmp(rebuilt.data() + Array::kCacheLineSize, page[1].data(), Array::kCacheLineSize) == 0);
}

TEST_CASE("FullyPersistentCacheArray: update() touches only the named cache line", "[fully_persistent_cache_array]") {
    constexpr uint64_t kPageSize = 128;
    using Array = FullyPersistentCacheArray<kPageSize>;

    std::array<CacheLine, Array::CacheLineAmount> page {
        MakeCacheLine(std::byte{0x01}),
        MakeCacheLine(std::byte{0x02}),
    };
    Array arr(1, 0, 0, page);

    CacheLine new_line_1 = MakeCacheLine(std::byte{0xFF});
    arr.update(1, 1, 1, /*cache_line_idx=*/1, new_line_1);

    PageMemory rebuilt = arr.get(1, 1);
    REQUIRE(std::memcmp(rebuilt.data(), page[0].data(), Array::kCacheLineSize) == 0);
    REQUIRE(std::memcmp(rebuilt.data() + Array::kCacheLineSize, new_line_1.data(), Array::kCacheLineSize) == 0);
}

TEST_CASE("FullyPersistentCacheArray: diff-based update only records cache lines that actually changed", "[fully_persistent_cache_array]") {
    constexpr uint64_t kPageSize = 192; // 3 cache lines
    using Array = FullyPersistentCacheArray<kPageSize>;

    std::array<CacheLine, Array::CacheLineAmount> page_v1 {
        MakeCacheLine(std::byte{0x01}),
        MakeCacheLine(std::byte{0x02}),
        MakeCacheLine(std::byte{0x03}),
    };
    Array arr(1, 0, 0, page_v1);

    std::array<CacheLine, Array::CacheLineAmount> page_v2 = page_v1;
    page_v2[2] = MakeCacheLine(std::byte{0x99}); // only the last line changes

    arr.update(1, 1, 1, page_v2, page_v1);

    PageMemory rebuilt = arr.get(1, 1);
    REQUIRE(std::memcmp(rebuilt.data(), page_v1[0].data(), Array::kCacheLineSize) == 0);
    REQUIRE(std::memcmp(rebuilt.data() + Array::kCacheLineSize, page_v1[1].data(), Array::kCacheLineSize) == 0);
    REQUIRE(std::memcmp(rebuilt.data() + 2 * Array::kCacheLineSize, page_v2[2].data(), Array::kCacheLineSize) == 0);
}

TEST_CASE("FullyPersistentCacheArray: branched run inherits unmodified cache lines from its parent", "[fully_persistent_cache_array]") {
    constexpr uint64_t kPageSize = 128;
    using Array = FullyPersistentCacheArray<kPageSize>;

    std::array<CacheLine, Array::CacheLineAmount> page {
        MakeCacheLine(std::byte{0x10}),
        MakeCacheLine(std::byte{0x20}),
    };
    Array arr(1, 0, 0, page);

    CacheLine run1_line0_update = MakeCacheLine(std::byte{0x77});
    arr.update(1, 1, 1, 0, run1_line0_update);

    // run 2 branches off run 1 at run_seq 1 (after the update above) and only ever touches line 1
    arr.register_branch(2, 1, 1);
    CacheLine run2_line1_update = MakeCacheLine(std::byte{0xEE});
    arr.update(2, 0, 2, 1, run2_line1_update);

    PageMemory rebuilt = arr.get(2, 0);
    REQUIRE(std::memcmp(rebuilt.data(), run1_line0_update.data(), Array::kCacheLineSize) == 0);
    REQUIRE(std::memcmp(rebuilt.data() + Array::kCacheLineSize, run2_line1_update.data(), Array::kCacheLineSize) == 0);
}

TEST_CASE("FullyPersistentCacheArray: get() on a cache line never written leaves that region zeroed", "[fully_persistent_cache_array]") {
    constexpr uint64_t kPageSize = 128;
    using Array = FullyPersistentCacheArray<kPageSize>;

    Array arr {};
    arr.runs[1]; // run exists but has never recorded an update for either cache line

    PageMemory rebuilt = arr.get(1, 0);
    PageMemory zero {};
    REQUIRE(std::memcmp(rebuilt.data(), zero.data(), rebuilt.size()) == 0);
}
