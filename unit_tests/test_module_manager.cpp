#include <catch2/catch_test_macros.hpp>
#include "module_manager.h"
#include <algorithm>

using namespace chronoporia;

// RestoreDLLsAtSequence loops over *every* DLL ever tracked (process_module_history is a
// module-level static with no reset hook) and calls ExecutionTree::GetState(from)/GetState(to)
// on each of their trees. Unlike FullyPersistentArray, ExecutionTree::GetState has no "most
// recent prior value" fallback - it requires an exact (run_id, run_seq) node match and
// assert()-aborts the whole process if one isn't found. TrackAllCurrentDLLs (the function that
// would otherwise carry every untouched DLL forward at each tick) is an unimplemented stub, so
// in practice every DLL must be explicitly re-tracked at any tick another DLL is queried at.
//
// That makes independent TEST_CASEs unsafe here: a second test querying a sequence that an
// earlier test's DLL never reached aborts the process (verified - it does). So this is
// deliberately one TEST_CASE walking a single coherent timeline, re-tracking every still-loaded
// DLL at each tick so every tree has the node being asked for.
namespace {

HMODULE FakeModule(uintptr_t value) {
    return reinterpret_cast<HMODULE>(value);
}

bool Contains(const std::vector<DllInfo>& dlls, HMODULE handle) {
    return std::find_if(dlls.begin(), dlls.end(), [handle](const DllInfo& info) {
        return info.dll_handle == handle;
    }) != dlls.end();
}

}

TEST_CASE("RestoreDLLsAtSequence reports exactly the DLLs that need freeing across a timeline", "[module_manager]") {
    constexpr uint32_t run_id = 1;
    HMODULE dll_a = FakeModule(0x1000);
    HMODULE dll_b = FakeModule(0x2000);

    // tick 0: both loaded
    TrackDLL(dll_a, L"a.dll", 0, run_id, 0);
    TrackDLL(dll_b, L"b.dll", 0, run_id, 0);

    // tick 1: a unloads, b is re-tracked (carried forward) since it's unaffected
    UntrackDLL(dll_a, 1, run_id, 1);
    TrackDLL(dll_b, L"b.dll", 1, run_id, 1);

    // tick 2: a reloads, b is carried forward again
    TrackDLL(dll_a, L"a.dll", 2, run_id, 2);
    TrackDLL(dll_b, L"b.dll", 2, run_id, 2);

    SECTION("a DLL unloaded at the target but loaded at the source is reported, one still loaded at both is not") {
        std::vector<DllInfo> dlls_to_free = RestoreDLLsAtSequence(run_id, 0, run_id, 1);
        REQUIRE(Contains(dlls_to_free, dll_a));
        REQUIRE_FALSE(Contains(dlls_to_free, dll_b));
    }

    SECTION("moving forward onto a reload does not report it for freeing") {
        std::vector<DllInfo> dlls_to_free = RestoreDLLsAtSequence(run_id, 1, run_id, 2);
        REQUIRE_FALSE(Contains(dlls_to_free, dll_a));
        REQUIRE_FALSE(Contains(dlls_to_free, dll_b));
    }

    SECTION("moving backward off of a reload reports it for freeing") {
        std::vector<DllInfo> dlls_to_free = RestoreDLLsAtSequence(run_id, 2, run_id, 1);
        REQUIRE(Contains(dlls_to_free, dll_a));
        REQUIRE_FALSE(Contains(dlls_to_free, dll_b));
    }
}
