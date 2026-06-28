#include "event_log.h"
#include "Events/base_event.h"
#include "Events/shared_library_events.h"
#include "nt_wrappers.h"
#include "quill/LogMacros.h"
#include "trampoline.h"
#include "breakpoint_manager.h"
#include "thread_events.h"
#include "shared_library_events.h"
#include "windows/nt_wrappers.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <stack>

namespace {
    std::vector<chronoporia::Event> event_log;
    std::map<DWORD, std::stack<chronoporia::Event>> pending_thread_events;

    uint64_t replayed_global_seq = 0;
    
    // TODO: make this cleaner.  Is it worth using shared_ptr for this?
    // a map of thread id to the index of the pending replay finish.  The index is the index of the event log
    std::map<DWORD, std::stack<uint64_t>> pending_replay_end_events;

    void AddEvent(chronoporia::Event new_event) {
        auto itr = std::upper_bound(event_log.begin(), event_log.end(), new_event->global_seq,             
            [](uint64_t seq, const chronoporia::Event& logged_event) {
                return seq < logged_event->global_seq;
            }
        );
        event_log.insert(itr, std::move(new_event));
    }
}

namespace chronoporia {

    void LogEvent(Event event) {
        AddEvent(std::move(event));
    }

    void OnBreakpointEnter(const uintptr_t rip_address, const DWORD thread_id) {
        CONTEXT ctx = RedirectToTrampoline(rip_address, thread_id);
        uintptr_t return_address;
        // Get the return addresss to place a breakpoint
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void*>(ctx.Rsp), &return_address, 8, nullptr);
        CreateReturnBreakpoint(return_address, thread_id);

        auto event = CreateEventFromBpAddress(rip_address, thread_id, ctx);
        if (event == nullptr) return;

        pending_thread_events[thread_id].push(std::move(event));
    }

    // TODO: write a verifier (or assert?) that checks if the address was a return address to the initial breakpoint for the thread
    //  This is in case events for some reason come out of order (or maybe async?)
    void OnBreakpointReturn(const uintptr_t rip_address, const DWORD thread_id, const CONTEXT& ctx) {
        auto& thread_event_stack = pending_thread_events[thread_id];
        if (thread_event_stack.empty()) return;

        auto completed_event = std::move(thread_event_stack.top());
        completed_event->FinishEvent(ctx);

        AddEvent(std::move(completed_event));
        thread_event_stack.pop();

        RemoveBreakpoint(rip_address, thread_id);
    }

    // TODO: change this to a dispatch table later
    Event CreateEventFromBpAddress(const uintptr_t rip_address, const DWORD thread_id, const CONTEXT& ctx) {
        void *bp_address = reinterpret_cast<void *>(rip_address);
        if (bp_address == NtCreateThreadEx) {
            return std::make_unique<ThreadCreateEvent>(thread_id, rip_address, ctx);
        } else if (bp_address == NtTerminateThread) {
            return std::make_unique<ThreadDestroyEvent>(thread_id, rip_address, ctx);
        } else if (bp_address == LdrLoadDll) {
            return std::make_unique<SharedLibraryLoadEvent>(thread_id, rip_address, ctx);
        } else if (bp_address == LdrUnloadDll) {
            return std::make_unique<SharedLibraryUnloadEvent>(thread_id, rip_address, ctx);
        }
        return nullptr;
    }

    void ReplayEvent(const uintptr_t rip_address, const DWORD thread_id) {
        auto it = std::find_if(event_log.begin() + replayed_global_seq, event_log.end(), [rip_address](Event &event) {
            return event->event_rip == rip_address;
        });

        if (it == event_log.end()) {
            LOG_WARNING(globals::logger, "Could not find event at address {}", rip_address);
            return;
        }

        auto idx = it - event_log.begin();
        auto &event = *event_log[idx];
        event.ReplayEvent();

        if (event.replay_kind == ReplayKind::Execute) {
            pending_replay_end_events[thread_id].push(idx);
        }

        CONTEXT ctx = RedirectToTrampoline(rip_address, thread_id);
        uintptr_t return_address;
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void*>(ctx.Rsp), &return_address, 8, nullptr);
        CreateReturnBreakpoint(return_address, thread_id);
    }

    void ReplayEventEnd(const uintptr_t rip_address, const DWORD thread_id) {
        auto& replay_event_stack = pending_replay_end_events[thread_id];
        if (replay_event_stack.empty()) return;

        uint64_t idx = replay_event_stack.top();

        event_log[idx]->ReplayEventEnd();
        replay_event_stack.pop();
        
        RemoveBreakpoint(rip_address, thread_id);
    }

}