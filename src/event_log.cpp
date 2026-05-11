#include "event_log.h"
#include "Events/base_event.h"
#include "Events/shared_library_events.h"
#include "Events/snapshot_events.h"
#include "nt_wrappers.h"
#include "trampoline.h"
#include "breakpoint_manager.h"
#include "thread_events.h"
#include "shared_library_events.h"
#include "windows/nt_wrappers.h"
#include <iterator>
#include <vector>
#include <map>
#include <stack>

namespace {
    std::vector<chronoporia::Event> event_log;
    std::map<DWORD, std::stack<chronoporia::Event>> thread_events;
}

namespace chronoporia {

    void LogEvent(Event event) {
        event_log.push_back(std::move(event));
    }

    void OnBreakpointEnter(const uintptr_t address, const DWORD thread_id) {
        CONTEXT ctx = RedirectToTrampoline(address, thread_id);
        uintptr_t return_address;
        // Get the return addresss to place a breakpoint
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void*>(ctx.Rsp), &return_address, 8, nullptr);
        CreateReturnBreakpoint(return_address, thread_id);

        auto event = CreateEventFromBpAddress(address, thread_id, ctx);
        if (event == nullptr) return;

        thread_events[thread_id].push(std::move(event));
    }

    // TODO: write a verifier (or assert?) that checks if the address was a return address to the initial breakpoint for the thread
    //  This is in case events for some reason come out of order (or maybe async?)
    // TODO: Make the event log use an insertion sort (?) since events can come out of order due to the stack
    void OnBreakpointReturn(const uintptr_t address, const DWORD thread_id, const CONTEXT& ctx) {
        auto& thread_event_stack = thread_events[thread_id];
        if (thread_event_stack.empty()) return;

        auto completed_event = std::move(thread_event_stack.top());
        completed_event->FinishEvent(ctx);

        event_log.push_back(std::move(completed_event));
        thread_event_stack.pop();

        RemoveBreakpoint(address, thread_id);
    }

    // TODO: change this to a dispatch table later
    Event CreateEventFromBpAddress(const uintptr_t address, const DWORD thread_id, const CONTEXT& ctx) {
        void *bp_address = reinterpret_cast<void *>(address);
        if (bp_address == NtCreateThreadEx) {
            return std::make_unique<ThreadCreateEvent>(thread_id, ctx);
        } else if (bp_address == NtTerminateThread) {
            return std::make_unique<ThreadDestroyEvent>(thread_id, ctx);
        } else if (bp_address == LdrLoadDll) {
            return std::make_unique<SharedLibraryLoadEvent>(thread_id, ctx);
        } else if (bp_address == LdrUnloadDll) {
            return std::make_unique<SharedLibraryUnloadEvent>(thread_id, ctx);
        }
        return nullptr;
    }

    uint64_t GetMostRecentCoarseEvent() {
        auto it = std::find_if(
            std::reverse_iterator(event_log.end()), 
            std::reverse_iterator(event_log.begin()), 
            [](const std::unique_ptr<BaseEvent>& p) {
                return dynamic_cast<CoarseSnapshotEvent*>(p.get()) != nullptr;
            }
        );
        return it->get()->global_seq;        
    }


    void ReplayFunctionEvent() {}

    void StubFunctionEvent() {}

    void JumpToEvent(const uint64_t event_seq) {
        event_log[event_seq]->apply();
    }
}