#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include "fully_persistent_region.h"
#include <cstdint>
#include <cstring>
#include <string>

using chronoporia::FullyPersistentRegion;
using chronoporia::PageLine;

namespace {

std::vector<std::byte> make_region(std::byte fill, uint32_t times) {
    std::vector<std::byte> line;
    line.resize(4096 * times);
    std::fill(line.begin(), line.end(), fill);
    return line;    
}

std::vector<std::byte> make_three_pages(std::byte fill, std::byte fill_2, std::byte fill_3) {
    std::vector<std::byte> line;
    line.resize(4096 * 3);
    std::fill(line.begin(), line.begin() + 4096, fill);
    std::fill(line.begin() + 4096, line.begin() + 8192, fill_2);
    std::fill(line.begin() + 8192, line.end(), fill_3);
    return line;  
}

std::vector<std::byte> make_one_page(std::byte fill) {
    std::vector<std::byte> line;
    line.resize(4096);
    std::fill(line.begin(), line.end(), fill);
    return line;  
}

std::vector<std::byte> make_five_pages(std::byte fill, std::byte fill_2, std::byte fill_3, std::byte fill_4, std::byte fill_5) {
    std::vector<std::byte> line;
    line.resize(4096 * 5);
    std::fill(line.begin(), line.begin() + 4096, fill);
    std::fill(line.begin() + 4096, line.begin() + 8192, fill_2);
    std::fill(line.begin() + 8192, line.begin() + 12288, fill_3);
    std::fill(line.begin() + 12288, line.begin() + 16384, fill_4);
    std::fill(line.begin() + 16384, line.end(), fill_5);
    return line;  
}

bool region_equal(std::span<const std::byte> region_1, std::span<const std::byte> region_2) {
    return memcmp(region_1.data(), region_2.data(), region_1.size()) == 0;
}

auto zero_byte = std::byte {0x00};
auto dummy_byte_1 = std::byte {0xCC};
auto dummy_byte_2 = std::byte {0x10};
}

TEST_CASE("FullyPersistentRegion: construction is readable at its own (run, seq)", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 2};
    auto region = make_region(dummy_byte_1, 2);
    arr.update(1, 0, 0, region);
    REQUIRE(region_equal(arr.get_region_at_time(1, 0, 4096 * 2), region));
}

TEST_CASE("FullyPersistentRegion: update within a run is visible at and after its run_seq", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 2};

    auto region = make_region(dummy_byte_1, 2);
    auto region_2 = make_region(dummy_byte_2, 2);

    arr.update(1, 0, 0, region);
    arr.update(1, 1, 1, region_2);

    REQUIRE(region_equal(arr.get_region_at_time(1, 0, 4096 * 2), region));
    REQUIRE(region_equal(arr.get_region_at_time(1, 1, 4096 * 2), region_2));
}

TEST_CASE("FullyPersistentRegion: unchanged data finds current data in sequence", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 3};

    auto region = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_1);
    auto region_2 = make_three_pages(dummy_byte_1, dummy_byte_2, dummy_byte_1);
    auto region_3 = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_2);

    arr.update(1, 0, 0, region);
    arr.update(1, 1, 1, region_2);
    arr.update(1, 2, 2, region_3);

    REQUIRE(region_equal(arr.get_region_at_time(1, 0, 4096 * 3), region));
    REQUIRE(region_equal(arr.get_region_at_time(1, 1, 4096 * 3), region_2));
    REQUIRE(region_equal(arr.get_region_at_time(1, 2, 4096 * 3), region_3));
}

TEST_CASE("FullyPersistentRegion: making empty new branch still finds previous node", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 3};

    auto region = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_1);
    arr.update(1, 0, 0, region);

    arr.make_branch(1, 0, 2);
    REQUIRE(region_equal(arr.get_region_at_time(2, 0, 4096 * 3), region));
}

TEST_CASE("FullyPersistentRegion: unchanged data finds current data across branches", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 3};

    auto region = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_1);
    auto region_2 = make_three_pages(dummy_byte_1, dummy_byte_2, dummy_byte_1);
    auto region_3 = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_2);

    arr.update(1, 0, 0, region);
    arr.update(1, 1, 1, region_2);
    arr.update(1, 2, 2, region_3);

    arr.make_branch(1, 0, 2);
    arr.update(2, 0, 2, region_2);
    arr.update(2, 1, 2, region_3);

    arr.make_branch(2, 0, 3);
    arr.update(3, 0, 2, region_3);

    arr.make_branch(1, 2, 4);
    arr.update(4, 0, 2, region);

    REQUIRE(region_equal(arr.get_region_at_time(1, 2, 4096 * 3), region_3));
    REQUIRE(region_equal(arr.get_region_at_time(2, 0, 4096 * 3), region_2));
    REQUIRE(region_equal(arr.get_region_at_time(2, 1, 4096 * 3), region_3));
    REQUIRE(region_equal(arr.get_region_at_time(3, 0, 4096 * 3), region_3));
    REQUIRE(region_equal(arr.get_region_at_time(4, 0, 4096 * 3), region));
}

TEST_CASE("FullyPersistentRegion: returns empty data if target is before creation", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/1, /*global_seq=*/0, 4096 * 3};

    auto region = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_1);
    auto zero_region = make_three_pages(zero_byte, zero_byte, zero_byte);

    arr.update(1, 1, 0, region);

    REQUIRE(region_equal(arr.get_region_at_time(0, 0, 4096 * 3), zero_region));
    REQUIRE(region_equal(arr.get_region_at_time(1, 0, 4096 * 3), zero_region));
}

TEST_CASE("FullyPersistentRegion: new sequences and branches can grow and shrink", "[fully_persistent_region]") {
    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 3};

    uint32_t size_1 = 4096;
    uint32_t size_3 = 4096 * 3;
    uint32_t size_5 = 4096 * 5;

    auto region_1 = make_one_page(dummy_byte_1);

    auto region_3_1 = make_three_pages(dummy_byte_1, dummy_byte_2, dummy_byte_1);
    auto region_3_2 = make_three_pages(dummy_byte_1, dummy_byte_1, dummy_byte_2);

    auto region_5_1 = make_five_pages(dummy_byte_1, dummy_byte_1, dummy_byte_2, dummy_byte_2, dummy_byte_1);
    auto region_5_2 = make_five_pages(dummy_byte_2, dummy_byte_1, dummy_byte_2, dummy_byte_2, dummy_byte_2);

    arr.update(1, 0, 0, region_3_1);
    arr.update(1, 1, 1, region_3_2);
    arr.update(1, 2, 2, region_1);
    arr.update(1, 3, 2, region_5_1);

    arr.make_branch(1, 0, 2);
    arr.update(2, 0, 2, region_1);
    arr.update(2, 1, 2, region_3_1);
    arr.update(2, 2, 2, region_5_1);

    arr.make_branch(2, 0, 3);
    arr.update(3, 0, 2, region_5_1);
    arr.update(3, 1, 2, region_5_1);
    arr.update(3, 2, 2, region_1);

    arr.make_branch(1, 3, 4);
    arr.update(4, 0, 2, region_1);
    arr.update(4, 1, 2, region_5_2);
    arr.update(4, 2, 2, region_3_2);

    // We'll be getting the true size from the mbi in actual use so here I just supply the known size at that time
    REQUIRE(region_equal(arr.get_region_at_time(1, 0, size_3), region_3_1));
    REQUIRE(region_equal(arr.get_region_at_time(1, 1, size_3), region_3_2));
    REQUIRE(region_equal(arr.get_region_at_time(1, 2, size_1), region_1));
    REQUIRE(region_equal(arr.get_region_at_time(1, 3, size_5), region_5_1));

    REQUIRE(region_equal(arr.get_region_at_time(2, 0, size_1), region_1));
    REQUIRE(region_equal(arr.get_region_at_time(2, 1, size_3), region_3_1));
    REQUIRE(region_equal(arr.get_region_at_time(2, 2, size_5), region_5_1));

    REQUIRE(region_equal(arr.get_region_at_time(3, 0, size_5), region_5_1));
    REQUIRE(region_equal(arr.get_region_at_time(3, 1, size_5), region_5_1));
    REQUIRE(region_equal(arr.get_region_at_time(3, 2, size_1), region_1));

    REQUIRE(region_equal(arr.get_region_at_time(4, 0, size_1), region_1));
    REQUIRE(region_equal(arr.get_region_at_time(4, 1, size_5), region_5_2));
    REQUIRE(region_equal(arr.get_region_at_time(4, 2, size_3), region_3_2));
}

TEST_CASE("FullyPersistentRegion: can make regions with empty memory", "[fully_persistent_region]") {

    FullyPersistentRegion arr = {/*run_id=*/1, /*run_seq=*/0, /*global_seq=*/0, 4096 * 3};
    auto region_3_1 = make_three_pages(dummy_byte_1, dummy_byte_2, dummy_byte_1);
    std::vector<std::byte> empty_vec = {};
    arr.update(1, 0, 0, region_3_1);
    arr.update(1, 1, 1, empty_vec);
    arr.update(1, 2, 0, region_3_1);

    REQUIRE(region_equal(arr.get_region_at_time(1, 0, 4096 * 3), region_3_1));
    REQUIRE(arr.get_region_at_time(1, 1, 0).size() == 0);
    REQUIRE(region_equal(arr.get_region_at_time(1, 0, 4096 * 3), region_3_1));
}