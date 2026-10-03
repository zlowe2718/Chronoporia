#pragma once

#include "base_breakpoint_plugin.h"
#include <Windows.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace breakpoint_plugin {

    struct ThreadInfo {
        HANDLE thread_handle;
        DWORD thread_id;
    };

    // Maps a CV_AMD64_* register id (cvconst.h) to its slot in a x64 CONTEXT.
    // Only the GPRs conditions realistically need for variable locations are covered;
    // anything else is treated as unsupported and the variable is skipped.
    uint64_t get_register_from_id(const CONTEXT &context, DWORD cv_register_id);

    uintptr_t get_image_base(DWORD process_id, const std::wstring &dll_name);

    struct ModuleRange {
        uintptr_t base = 0;
        uint64_t size = 0;
    };

    ModuleRange get_module_range(DWORD process_id, const std::wstring &dll_name);

    // Walks the call stack of thread_id (in process_handle) starting from its current
    // context, and returns the first return address (AddrPC) that falls within range.
    // Needed because the thread's live Rip can be paused inside an unrelated module
    // (ntdll, ucrtbase, ...) when the frame we actually want to inspect is further down
    // the stack.
    std::optional<OriginalAddress> find_rip_in_module(HANDLE process_handle, DWORD thread_id, const ModuleRange &range);
    OriginalAddress get_return_address(const HANDLE process_handle, const DWORD thread_id);
    std::vector<OriginalAddress> get_all_return_addresses(const HANDLE process_handle, const DWORD thread_id);

    // Walks and prints the call stack of thread_id, or of every thread in process_id when
    // thread_id is 0.  Symbol/line information comes from the symbol handler that
    // initialize_plugin set up on process_handle.
    void print_stack(HANDLE process_handle, DWORD process_id, DWORD thread_id);

    // TODO: support the other registers
    void print_registers(DWORD thread_id, DWORD process_id);

    // Prints the source of file_path from current_line + start_offset through
    // current_line + end_offset (both inclusive, both signed so callers can ask for context
    // above the current line), marking current_line itself.  file_path is the compile time
    // path recorded in the pdb, so it may not exist on the machine running the debugger.
    void print_source_lines(const std::wstring &file_path, uint64_t current_line, int32_t start_offset, int32_t end_offset);

    std::wstring str_to_wstr(const std::string& str);
}