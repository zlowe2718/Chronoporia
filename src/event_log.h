#pragma once
#include "base_event.h"
#include <memory>

// TODO: I need a thread map to event stack then on stack pop I add it to the event log?
//  Probably need some kind of insertion sort then since events will come out of order

// TODO: I need a way to process events as they come in like event type A then I create event A, then on function return I finish event A and pop it from the stack

namespace chronoporia {
    using Event = std::unique_ptr<BaseEvent>;

    void LogEvent(Event event);
    // void ProcessEvent();

    void OnBreakpointEnter(const uintptr_t address, const DWORD thread_id);
    void OnBreakpointReturn(const uintptr_t address, const DWORD thread_id, const CONTEXT& ctx);

    Event CreateEventFromBpAddress(const uintptr_t address, const DWORD thread_id, const CONTEXT& ctx);
    
    uint64_t GetMostRecentCoarseEvent();

    // TODO: Implement.  This will be called during a non-deterministic event like NtAllocateVirtualMemory.  Since we'd need to replay
    // That function anyways, we'll pass in the parameters needed and let the OS handle it.
    void ReplayFunctionEvent();
    
    // TODO: Implement.  This will be called during a non-deterministic event like NtQueryPerformanceCounter or RtlGenRandom.
    // Since those return non-deterministic values we need to stub the function on replay and just supply the returned value
    void StubFunctionEvent();

    // TODO: implement.  Jump to an event in the event_log 
    void JumpToEvent(const uint64_t event_seq);
}