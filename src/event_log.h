#pragma once
#include "base_event.h"
#include <cstdint>
#include <memory>

namespace chronoporia {
    using Event = std::unique_ptr<BaseEvent>;

    void LogEvent(Event event);

    void OnBreakpointEnter(const uintptr_t rip_address, const DWORD thread_id);
    void OnBreakpointReturn(const uintptr_t rip_address, const DWORD thread_id, const CONTEXT& ctx);

    Event CreateEventFromBpAddress(const uintptr_t addrip_addressess, const DWORD thread_id, const CONTEXT& ctx);

    // Take in an address and replay or stub the next event in the log.  Look through the log in case replayed events
    //  are out of order from the event log
    void ReplayEvent(const uintptr_t rip_address, const DWORD thread_id);

    // Finish gathering information for event we need to re-execute like grabbing the out handle from NtCreateThreadEx
    void ReplayEventEnd(const uintptr_t rip_address, const DWORD thread_id);

    void ResetEventReplay(uint64_t global_seq);

    // TODO: implement.  Jump to an event in the event_log 
    void JumpToEvent(const uint64_t event_seq);
}