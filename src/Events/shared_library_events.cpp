#include "shared_library_events.h"
#include "quill/LogMacros.h"
#include <print>

namespace chronoporia {

    void SharedLibraryLoadEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        // ReadProcessMemory(globals::process_handle, dll_handle_out, &dll_handle, sizeof(HMODULE), nullptr);
        // TrackDLL(dll_handle, dll_name, global_seq);
    }

    void SharedLibraryLoadEvent::ReplayEvent() {
        LOG_DEBUG(globals::logger, "SharedLibraryLoad Event Replay called");
    }

    void SharedLibraryLoadEvent::ReplayEventEnd() {
        LOG_DEBUG(globals::logger,"SharedLibraryLoad Event Replay End called");
    }

    void SharedLibraryUnloadEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        // UntrackDLL(dll_handle, global_seq);
    }

    void SharedLibraryUnloadEvent::ReplayEvent() {
        LOG_DEBUG(globals::logger,"SharedLibraryUnload Event Replay called");
    }

    void SharedLibraryUnloadEvent::ReplayEventEnd() {
        LOG_DEBUG(globals::logger,"SharedLibraryUnload Event Replay End called");
    }

}