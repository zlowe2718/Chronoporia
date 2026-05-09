#include "shared_library_events.h"
// #include "module_manager.h"

namespace chronoporia {

    void SharedLibraryLoadEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        // ReadProcessMemory(globals::process_handle, dll_handle_out, &dll_handle, sizeof(HMODULE), nullptr);
        // TrackDLL(dll_handle, dll_name, global_seq);
    }


    void SharedLibraryUnloadEvent::FinishEvent(const CONTEXT& thread_ctx) {
        return_status = thread_ctx.Rax;

        // UntrackDLL(dll_handle, global_seq);
    }
}