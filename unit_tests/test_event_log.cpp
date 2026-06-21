#include <catch2/catch_test_macros.hpp>
#include "event_log.h"
#include "Events/base_event.h"
#include "Events/thread_events.h"
#include "Events/shared_library_events.h"
#include "nt_wrappers.h"
#include "globals.h"
#include <cstdint>
#include <cstring>

using namespace chronoporia;

namespace {

// CreateEventFromBpAddress dispatches purely on pointer identity against the resolved ntdll/Ldr
// exports, so the wrappers must actually be resolved once per test binary before any dispatch
// test runs (otherwise every unresolved pointer is nullptr and a rip_address of 0 would
// false-match).
struct WrapperAddressesFixture {
    WrapperAddressesFixture() {
        InitializeWrapperAddresses();
        globals::process_handle = GetCurrentProcess();
    }
    ~WrapperAddressesFixture() {
        globals::process_handle = 0;
    }
};

// A address that can never alias a real ntdll export - used to exercise the "unknown
// breakpoint" branch of CreateEventFromBpAddress.
__declspec(noinline) void NotABreakpointAddress() {}

// Minimal concrete BaseEvent so LogEvent/ReplayEvent/ReplayEventEnd can be exercised without
// touching any of the real Nt*/Ldr* event subclasses, whose FinishEvent/ReplayEvent
// implementations assume a live remote debuggee (duplicating handles out of a child process,
// tracking real threads, etc). Capturing the raw pointer before moving it into LogEvent lets the
// test observe which methods event_log actually invoked, since event_log takes ownership.
struct TestEvent : BaseEvent {
    bool finish_called = false;
    bool replay_called = false;
    bool replay_end_called = false;

    TestEvent(DWORD thread_id, uintptr_t event_rip, ReplayKind kind = ReplayKind::Execute)
        : BaseEvent(thread_id, event_rip)
    {
        replay_kind = kind;
    }

    void FinishEvent(const CONTEXT&) override { finish_called = true; }
    void ReplayEvent() override { replay_called = true; }
    void ReplayEventEnd() override { replay_end_called = true; }
};

// event_log.cpp keeps its log/pending-stacks as module-level statics with no reset hook, so
// every TEST_CASE here uses its own fake rip address and thread id (well outside the range of
// real exported addresses) to stay independent of whatever earlier tests logged.
uintptr_t FakeAddress(uint64_t salt) {
    return static_cast<uintptr_t>(0x10000 + salt * 0x10);
}

}

TEST_CASE_METHOD(WrapperAddressesFixture, "CreateEventFromBpAddress returns nullptr for an address that matches no tracked breakpoint", "[event_log]") {
    uintptr_t addr = reinterpret_cast<uintptr_t>(&NotABreakpointAddress);
    CONTEXT ctx {};

    Event event = CreateEventFromBpAddress(addr, /*thread_id*/1, ctx);
    REQUIRE(event == nullptr);
}

TEST_CASE_METHOD(WrapperAddressesFixture, "CreateEventFromBpAddress at NtCreateThreadEx builds a ThreadCreateEvent with its in-args and stack args read from the context", "[event_log]") {
    ThreadCreateStackArgs expected_stack_args {};
    expected_stack_args.start_routine = reinterpret_cast<PUSER_THREAD_START_ROUTINE>(0x1111);
    expected_stack_args.argument = reinterpret_cast<PVOID>(0x2222);
    expected_stack_args.create_flags = 0x3;
    expected_stack_args.zero_bits = 0x4;
    expected_stack_args.stack_size = 0x5000;
    expected_stack_args.maximum_stack_size = 0x6000;
    expected_stack_args.attribute_list = nullptr;

    HANDLE thread_handle_storage = nullptr;
    HANDLE process_handle_value = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x4444));

    CONTEXT ctx {};
    ctx.Rcx = reinterpret_cast<DWORD64>(&thread_handle_storage);
    ctx.Rdx = 0x12345;
    ctx.R8 = 0; // object_attributes - never dereferenced by the constructor
    ctx.R9 = reinterpret_cast<DWORD64>(process_handle_value);
    ctx.Rsp = reinterpret_cast<DWORD64>(&expected_stack_args);

    Event event = CreateEventFromBpAddress(reinterpret_cast<uintptr_t>(NtCreateThreadEx), /*thread_id*/1, ctx);

    auto* create_event = dynamic_cast<ThreadCreateEvent*>(event.get());
    REQUIRE(create_event != nullptr);
    REQUIRE(create_event->thread_handle == &thread_handle_storage);
    REQUIRE(create_event->desired_access == 0x12345);
    REQUIRE(create_event->process_handle == process_handle_value);
    REQUIRE(create_event->stack_args.start_routine == expected_stack_args.start_routine);
    REQUIRE(create_event->stack_args.argument == expected_stack_args.argument);
    REQUIRE(create_event->stack_args.create_flags == expected_stack_args.create_flags);
    REQUIRE(create_event->stack_args.zero_bits == expected_stack_args.zero_bits);
    REQUIRE(create_event->stack_args.stack_size == expected_stack_args.stack_size);
    REQUIRE(create_event->stack_args.maximum_stack_size == expected_stack_args.maximum_stack_size);
}

TEST_CASE_METHOD(WrapperAddressesFixture, "CreateEventFromBpAddress at NtTerminateThread builds a ThreadDestroyEvent that resolves the real thread id from the handle", "[event_log]") {
    HANDLE real_handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    REQUIRE(real_handle != nullptr);

    CONTEXT ctx {};
    ctx.Rcx = reinterpret_cast<DWORD64>(real_handle);
    ctx.Rdx = 0; // exit_status

    Event event = CreateEventFromBpAddress(reinterpret_cast<uintptr_t>(NtTerminateThread), /*thread_id*/1, ctx);

    auto* destroy_event = dynamic_cast<ThreadDestroyEvent*>(event.get());
    REQUIRE(destroy_event != nullptr);
    REQUIRE(destroy_event->thread_handle_ == real_handle);
    REQUIRE(destroy_event->thread_id_ == GetCurrentThreadId());

    CloseHandle(real_handle);
}

TEST_CASE_METHOD(WrapperAddressesFixture, "CreateEventFromBpAddress at LdrUnloadDll builds a SharedLibraryUnloadEvent with the handle from the context", "[event_log]") {
    HMODULE fake_module = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x9999));

    CONTEXT ctx {};
    ctx.Rcx = reinterpret_cast<DWORD64>(fake_module);

    Event event = CreateEventFromBpAddress(reinterpret_cast<uintptr_t>(LdrUnloadDll), /*thread_id*/1, ctx);

    auto* unload_event = dynamic_cast<SharedLibraryUnloadEvent*>(event.get());
    REQUIRE(unload_event != nullptr);
    REQUIRE(unload_event->dll_handle == fake_module);
}

TEST_CASE_METHOD(WrapperAddressesFixture, "CreateEventFromBpAddress at LdrLoadDll builds a SharedLibraryLoadEvent with fields read from the context", "[event_log]") {
    // dll_path is only ever filled with the first 130 wide chars of whatever's at Rcx (the
    // constructor reads 260 *bytes* into a 260-wchar_t buffer) - a short, null-padded source
    // string keeps the comparison simple regardless of that quirk.
    wchar_t path_buffer[200] = {};
    wcscpy_s(path_buffer, L"C:\\fake\\path.dll");

    ULONG expected_characteristics = 0xAB;

    wchar_t name_buffer[64] = {};
    wcscpy_s(name_buffer, L"path.dll");
    UNICODE_STRING name_unicode {};
    name_unicode.Length = static_cast<USHORT>(wcslen(name_buffer) * sizeof(wchar_t));
    name_unicode.MaximumLength = name_unicode.Length;
    name_unicode.Buffer = name_buffer;

    HMODULE dll_handle_storage = nullptr;

    CONTEXT ctx {};
    ctx.Rcx = reinterpret_cast<DWORD64>(path_buffer);
    ctx.Rdx = reinterpret_cast<DWORD64>(&expected_characteristics);
    ctx.R8 = reinterpret_cast<DWORD64>(&name_unicode);
    ctx.R9 = reinterpret_cast<DWORD64>(&dll_handle_storage);

    Event event = CreateEventFromBpAddress(reinterpret_cast<uintptr_t>(LdrLoadDll), /*thread_id*/1, ctx);

    auto* load_event = dynamic_cast<SharedLibraryLoadEvent*>(event.get());
    REQUIRE(load_event != nullptr);
    REQUIRE(load_event->dll_path.substr(0, 16) == L"C:\\fake\\path.dll");
    REQUIRE(load_event->dll_characteristics == expected_characteristics);
    REQUIRE(load_event->dll_name.substr(0, 8) == L"path.dll");
    REQUIRE(load_event->dll_handle_out == &dll_handle_storage);
}

TEST_CASE("LogEvent inserts events so ReplayEvent finds the matching one by rip address and queues it for ReplayEventEnd", "[event_log]") {
    uintptr_t addr_a = FakeAddress(1);
    uintptr_t addr_b = FakeAddress(2);
    DWORD thread_id = 90001;

    auto event_a = std::make_unique<TestEvent>(thread_id, addr_a);
    auto event_b = std::make_unique<TestEvent>(thread_id, addr_b);
    TestEvent* raw_a = event_a.get();
    TestEvent* raw_b = event_b.get();

    LogEvent(std::move(event_a));
    LogEvent(std::move(event_b));

    ReplayEvent(addr_a, thread_id);
    REQUIRE(raw_a->replay_called);
    REQUIRE_FALSE(raw_b->replay_called);

    // Default replay_kind is Execute, so the matched event should be queued for ReplayEventEnd.
    ReplayEventEnd(addr_a, thread_id);
    REQUIRE(raw_a->replay_end_called);
    REQUIRE_FALSE(raw_b->replay_end_called);
}

TEST_CASE("ReplayEvent on a Stub-kind event does not register a pending ReplayEventEnd", "[event_log]") {
    uintptr_t addr = FakeAddress(3);
    DWORD thread_id = 90002;

    auto event = std::make_unique<TestEvent>(thread_id, addr, ReplayKind::Stub);
    TestEvent* raw = event.get();
    LogEvent(std::move(event));

    ReplayEvent(addr, thread_id);
    REQUIRE(raw->replay_called);

    // Nothing was pushed onto the pending-replay-end stack for a Stub event, so this is a no-op.
    ReplayEventEnd(addr, thread_id);
    REQUIRE_FALSE(raw->replay_end_called);
}

TEST_CASE("ReplayEvent for an address that was never logged does not crash", "[event_log]") {
    DWORD thread_id = 90003;
    REQUIRE_NOTHROW(ReplayEvent(FakeAddress(4), thread_id));
    REQUIRE_NOTHROW(ReplayEventEnd(FakeAddress(4), thread_id));
}
