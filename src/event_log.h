#pragma once
#include "base_event.h"
#include <memory>

namespace chronoporia {
    using Event = std::unique_ptr<BaseEvent>;

    void LogEvent(Event event);

    void OnBreakpointEnter(const uintptr_t address, const DWORD thread_id);
    void OnBreakpointReturn(const uintptr_t address, const DWORD thread_id, const CONTEXT& ctx);

    Event CreateEventFromBpAddress(const uintptr_t address, const DWORD thread_id, const CONTEXT& ctx);
    
    uint64_t GetMostRecentCoarseEvent();
}