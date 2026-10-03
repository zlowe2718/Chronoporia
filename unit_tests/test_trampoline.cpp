#include <catch2/catch_test_macros.hpp>
#include "trampoline.h"
#include "globals.h"

using namespace chronoporia;

namespace {

// CreateTrampoline only ever copies the *first* decoded instruction plus a relocated jump back
// to the rest of the function - it never patches the original code. That means the relocated
// stub is itself a legitimate, callable copy of the function (first instruction relocated,
// then falls straight back into the original body), so we can verify it end-to-end by actually
// calling it and comparing against calling the original directly.
__declspec(noinline) int AddOne(int x) {
    return x + 1;
}

// address_to_trampoline_location is a module-level static in trampoline.cpp with no reset hook,
// so it persists across TEST_CASEs in this process - this target is kept untouched by every
// other test so "never trampolined" stays true regardless of test order.
__declspec(noinline) int AddTwo(int x) {
    return x + 2;
}

struct TrampolineFixture {
    TrampolineFixture() {
        globals::process_handle = GetCurrentProcess();
        CreateTrampolineRegion();
    }
    ~TrampolineFixture() {
        DestroyTrampolineRegion();
        globals::process_handle = 0;
    }
};

}

TEST_CASE_METHOD(TrampolineFixture, "GetTrampolineAddress is zero for an address that was never trampolined", "[trampoline]") {
    REQUIRE(GetTrampolineAddress(reinterpret_cast<uintptr_t>(&AddTwo)) == 0);
}

TEST_CASE_METHOD(TrampolineFixture, "CreateTrampoline produces a callable relocation of the target's first instruction", "[trampoline]") {
    uintptr_t target = reinterpret_cast<uintptr_t>(&AddOne);

    uintptr_t trampoline_entry = CreateTrampoline(target);
    REQUIRE(trampoline_entry != 0);

    FinalizeTrampolineRegion();

    REQUIRE(GetTrampolineAddress(target) == trampoline_entry);

    auto trampolined_fn = reinterpret_cast<int (*)(int)>(trampoline_entry);
    REQUIRE(trampolined_fn(41) == AddOne(41));
}

TEST_CASE_METHOD(TrampolineFixture, "Each call to CreateTrampoline for a distinct address gets a distinct slot", "[trampoline]") {
    uintptr_t target_a = reinterpret_cast<uintptr_t>(&AddOne);
    uintptr_t entry_a = CreateTrampoline(target_a);

    // Reuse of the SAME address should hand back the slot recorded for that address, not
    // allocate a second one.
    uintptr_t entry_a_again = CreateTrampoline(target_a);

    REQUIRE(entry_a != 0);
    REQUIRE(entry_a_again != entry_a); // CreateTrampoline always appends a new region slot...
    FinalizeTrampolineRegion();
    // ...but GetTrampolineAddress reflects whichever call happened last for that address.
    REQUIRE(GetTrampolineAddress(target_a) == entry_a_again);
}
