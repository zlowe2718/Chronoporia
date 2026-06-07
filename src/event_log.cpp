#include "event_log.h"
#include "Events/base_event.h"
#include "Events/shared_library_events.h"
#include "nt_wrappers.h"
#include "trampoline.h"
#include "breakpoint_manager.h"
#include "thread_events.h"
#include "shared_library_events.h"
#include "windows/nt_wrappers.h"
#include <map>
#include <stack>

namespace {
    std::vector<chronoporia::Event> event_log;
    std::map<DWORD, std::stack<chronoporia::Event>> pending_thread_events;

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

    void ReplayEvent(const uintptr_t rip_address) {
        
    }

}