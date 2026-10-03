#include <catch2/catch_test_macros.hpp>
#include "breakpoint_manager.h"
#include "trampoline.h"
#include "globals.h"

using namespace chronoporia;

namespace {

// CreatePermanentBreakpoint needs a real, callable function to build a trampoline for - it must
// never be called while a breakpoint is installed on it. Using our own throwaway helper (rather
// than a live ntdll entry point the process actually depends on) means the only thing that could
// ever execute into the patched byte is this test itself, so there's no risk of crashing the
// whole test binary on an unhandled breakpoint exception.
__declspec(noinline) int Dummy(int x) {
    return x + 3;
}

struct ScratchFixture {
    void *scratch = nullptr;

    ScratchFixture() {
        globals::process_handle = GetCurrentProcess();
        scratch = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        memset(scratch, 0x90, 4096); // fill with NOPs as a known, inert "original" byte
    }
    ~ScratchFixture() {
        VirtualFree(scratch, 0, MEM_RELEASE);
        globals::process_handle = 0;
    }

    uintptr_t AddressAt(size_t offset) const {
        return reinterpret_cast<uintptr_t>(scratch) + offset;
    }

    BYTE ByteAt(size_t offset) const {
        return *(reinterpret_cast<BYTE *>(scratch) + offset);
    }
};

struct TrampolineFixture : ScratchFixture {
    TrampolineFixture() { CreateTrampolineRegion(); }
    ~TrampolineFixture() { DestroyTrampolineRegion(); }
};

}

TEST_CASE_METHOD(ScratchFixture, "WriteBreakpoint patches a 0xCC and RestoreMemory puts the original byte back", "[breakpoint_manager]") {
    uintptr_t addr = AddressAt(0);
    REQUIRE(ByteAt(0) == 0x90);

    WriteBreakpoint(addr, BreakpointType::Return);
    REQUIRE(ByteAt(0) == static_cast<BYTE>(int3));

    REQUIRE(RestoreMemory(Breakpoint{0x90, BreakpointType::Return, 1, addr, {}}));
    REQUIRE(ByteAt(0) == 0x90);
}

TEST_CASE_METHOD(ScratchFixture, "CreateReturnBreakpoint is per-thread: only a holding thread sees Return, others see SpinLock", "[breakpoint_manager]") {
    uintptr_t addr = AddressAt(8);
    DWORD holder = 111;
    DWORD other = 222;

    CreateReturnBreakpoint(addr, holder);
    REQUIRE(ByteAt(8) == static_cast<BYTE>(int3));
    REQUIRE(GetBreakpointType(addr, holder) == BreakpointType::Return);
    REQUIRE(GetBreakpointType(addr, other) == BreakpointType::SpinLock);

    // a second thread can also hold the same return breakpoint
    CreateReturnBreakpoint(addr, other);
    REQUIRE(GetBreakpointType(addr, other) == BreakpointType::Return);

    // removing one holder leaves the breakpoint installed for the remaining holder
    REQUIRE(RemoveBreakpoint(addr, holder));
    REQUIRE(ByteAt(8) == static_cast<BYTE>(int3));
    REQUIRE(GetBreakpointType(addr, other) == BreakpointType::Return);

    // removing the last holder restores the original byte
    REQUIRE(RemoveBreakpoint(addr, other));
    REQUIRE(ByteAt(8) == 0x90);

    // and the address is no longer tracked at all
    REQUIRE_FALSE(RemoveBreakpoint(addr, other));
    REQUIRE(GetBreakpointType(addr, other) == BreakpointType::SpinLock);
}

TEST_CASE_METHOD(ScratchFixture, "RemoveBreakpoint refuses a thread that never held the breakpoint", "[breakpoint_manager]") {
    uintptr_t addr = AddressAt(16);
    CreateReturnBreakpoint(addr, /*holder=*/333);

    REQUIRE_FALSE(RemoveBreakpoint(addr, /*not a holder*/444));
    REQUIRE(ByteAt(16) == static_cast<BYTE>(int3)); // still installed

    REQUIRE(RemoveBreakpoint(addr, 333));
    REQUIRE(ByteAt(16) == 0x90);
}

TEST_CASE_METHOD(ScratchFixture, "TrackShellCodeBreakpoint reports ShellCode regardless of which thread asks", "[breakpoint_manager]") {
    uintptr_t addr = AddressAt(24);

    // The shellcode breakpoint byte is expected to already be present in the target by the
    // shellcode itself, so TrackShellCodeBreakpoint is pure bookkeeping and never touches memory.
    TrackShellCodeBreakpoint(addr);

    REQUIRE(ByteAt(24) == 0x90);
    REQUIRE(GetBreakpointType(addr, 1) == BreakpointType::ShellCode);
    REQUIRE(GetBreakpointType(addr, 2) == BreakpointType::ShellCode);
}

TEST_CASE_METHOD(TrampolineFixture, "CreatePermanentBreakpoint/RemovePermanentBreakpoint round-trip leaves the function callable again", "[breakpoint_manager]") {
    uintptr_t addr = reinterpret_cast<uintptr_t>(&Dummy);

    CreatePermanentBreakpoint(addr);
    REQUIRE(GetBreakpointType(addr, /*any thread*/1) == BreakpointType::Permanent);

    // permanent breakpoints can't be removed via the per-thread removal path
    REQUIRE_FALSE(RemoveBreakpoint(addr, 1));

    REQUIRE(RemovePermanentBreakpoint(addr));
    REQUIRE(GetBreakpointType(addr, 1) == BreakpointType::SpinLock);

    // byte is genuinely restored - the function executes correctly again
    REQUIRE(Dummy(5) == 8);
}

TEST_CASE_METHOD(TrampolineFixture, "RemoveAllPermanentBreakpoints restores every installed permanent breakpoint", "[breakpoint_manager]") {
    uintptr_t addr_a = AddressAt(0);
    uintptr_t addr_b = AddressAt(8);

    CreatePermanentBreakpoint(addr_a);
    CreatePermanentBreakpoint(addr_b);
    REQUIRE(ByteAt(0) == static_cast<BYTE>(int3));
    REQUIRE(ByteAt(8) == static_cast<BYTE>(int3));

    REQUIRE(RemoveAllPermanentBreakpoints());

    REQUIRE(ByteAt(0) == 0x90);
    REQUIRE(ByteAt(8) == 0x90);
    REQUIRE(GetBreakpointType(addr_a, 1) == BreakpointType::SpinLock);
    REQUIRE(GetBreakpointType(addr_b, 1) == BreakpointType::SpinLock);
}
