#include <catch2/catch_test_macros.hpp>
#include "memory_manager.h"
#include "globals.h"
#include <optional>

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

    globals::process_handle = 0;
}
