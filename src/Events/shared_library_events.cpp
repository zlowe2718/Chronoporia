#include "shared_library_events.h"
#include <print>

namespace chronoporia {

    void SharedLibraryLoadEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        // ReadProcessMemory(globals::process_handle, dll_handle_out, &dll_handle, sizeof(HMODULE), nullptr);
        // TrackDLL(dll_handle, dll_name, global_seq);
    }

    void SharedLibraryLoadEvent::ReplayEvent() {
        std::print("\nSharedLibraryLoad Event Replay called\n");
    }

    void SharedLibraryLoadEvent::ReplayEventEnd() {
        std::print("\nSharedLibraryLoad Event Replay End called\n");
    }

    void SharedLibraryUnloadEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        // UntrackDLL(dll_handle, global_seq);
    }

    void SharedLibraryUnloadEvent::ReplayEvent() {
        std::print("\nSharedLibraryUnload Event Replay called\n");
    }

    void SharedLibraryUnloadEvent::ReplayEventEnd() {
        std::print("\nSharedLibraryUnload Event Replay End called\n");
    }

}