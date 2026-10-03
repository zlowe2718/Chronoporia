#include "breakpoint_manager.h"
#include "globals.h"
#include "plugin_manager.h"
#include "quill/LogMacros.h"
#include "trampoline.h"
#include <cstdint>
#include <optional>
#include <print>
#include <unordered_map>

namespace {
    std::unordered_map<uintptr_t, chronoporia::Breakpoint> address_to_bp;
    std::unordered_map<uintptr_t, uintptr_t> permanent_bp_to_trampoline;
    // This is a map of true address to the breakpoint.  True address is where the instruction actually lives
    // (if it was remapped by a trampoline)
    std::unordered_map<uintptr_t, chronoporia::UserBreakpoint> user_breakpoints;
}

namespace chronoporia {

void CreateUserBreakpoint(uintptr_t address, const std::string& condition) {
    BYTE original;
    uintptr_t true_address = get_true_address("cpp", address);
    ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(true_address), &original, 1, nullptr);
    WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(true_address), &int3, 1, nullptr);
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(true_address), 1);
    user_breakpoints[true_address] = UserBreakpoint{original, true, address, true_address, 0, 0, "", condition};
}

void CreatePermanentBreakpoint(uintptr_t address) {
    uintptr_t trampoline_address = CreateTrampoline(address);
    WriteBreakpoint(address, BreakpointType::Permanent);
    permanent_bp_to_trampoline[address] = trampoline_address;
}

void CreateReturnBreakpoint(uintptr_t address, const DWORD thread_id) {
    if (address_to_bp.contains(address)) {
        auto& bp = address_to_bp[address];
        bp.holding_threads.push_back(thread_id);
        bp.count += 1;
        return;
    }

    WriteBreakpoint(address, BreakpointType::Return);
    address_to_bp[address].holding_threads.push_back(thread_id);
}

void create_step_breakpoint(uintptr_t address, const DWORD thread_id) {
    if (address_to_bp.contains(address)) {
        auto& bp = address_to_bp[address];
        bp.holding_threads.push_back(thread_id);
        bp.count += 1;
        return;
    }

    WriteBreakpoint(address, BreakpointType::Step);
    address_to_bp[address].holding_threads.push_back(thread_id);    
}

void create_reverse_breakpoint(uintptr_t address, const DWORD thread_id) {
    uintptr_t true_address = get_true_address("cpp", address);
    WriteBreakpoint(true_address, BreakpointType::Reverse);
    address_to_bp[true_address].holding_threads.push_back(thread_id);
}

void TrackShellCodeBreakpoint(uintptr_t address) {
    address_to_bp[address] = Breakpoint{ 0, BreakpointType::ShellCode, 0, address, {} };
}

bool RemoveBreakpoint(uintptr_t address, const DWORD thread_id) {
    if (permanent_bp_to_trampoline.contains(address)) return false;
    if (address_to_bp.contains(address)) {
        auto& bp = address_to_bp[address];

        // If thread doesn't hold the breakpoint return false
        auto itr = std::find(bp.holding_threads.begin(), bp.holding_threads.end(), thread_id);
        if (itr == bp.holding_threads.end()) return false;

        RestoreMemory(bp);

        // can't erase until memoru is restored since we grabbed a reference above
        if (bp.count - 1 == 0) {
            address_to_bp.erase(address);
        } else {
            bp.count -= 1;
            bp.holding_threads.erase(itr);
        }
        return true;
    }
    if (user_breakpoints.contains(address)) {
        return remove_user_breakpoint(address);
    }
    return false;
}

bool RemovePermanentBreakpoint(uintptr_t address) {
    if (!address_to_bp.contains(address)) {
        LOG_ERROR(globals::logger, "Address {:p} did not exist in the address_to_bp map when removing", address);
        return false;
    }
    const Breakpoint& bp = address_to_bp[address];
    bool bp_removed = RestoreMemory(bp);
    if (bp_removed) {
        address_to_bp.erase(address);
        permanent_bp_to_trampoline.erase(address);
        return true;
    }
    return false;
}

bool RemoveAllPermanentBreakpoints() {
    bool bp_removed = true;
    std::unordered_map<uintptr_t, uintptr_t> bp_to_remove = permanent_bp_to_trampoline;

    for (const auto& [address, _] : bp_to_remove) {
        bp_removed = RemovePermanentBreakpoint(address);
        if (!bp_removed) return false;
    }
    return bp_removed;
}

void WriteBreakpoint(uintptr_t address, BreakpointType bp_type) {
    BYTE original;
    ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(address), &original, 1, nullptr);
    WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(address), &int3, 1, nullptr);
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(address), 1);
    address_to_bp[address] = Breakpoint{ original, bp_type, 1, address, {} };
}

bool RestoreMemory(const Breakpoint& bp) {
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.address), &bp.saved_byte, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing original byte failed at address {:p} with error {}", bp.address, GetLastError());
        return false;
    }
    // Flush instruction cache so CPU sees the new byte
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.address), 1);
    return true;
}

std::optional<BreakpointType> GetBreakpointType(uintptr_t address, const DWORD thread_id) {
    if (user_breakpoints.contains(address)) {
        return BreakpointType::User;
    } else if (permanent_bp_to_trampoline.contains(address)) {
        return BreakpointType::Permanent;
    } else if (address_to_bp.contains(address)) {
        auto& bp = address_to_bp[address];

        // don't check thread for shellcode
        if (bp.kind == BreakpointType::ShellCode) return BreakpointType::ShellCode;

        // If thread doesn't hold the breakpoint return false
        auto itr = std::find(bp.holding_threads.begin(), bp.holding_threads.end(), thread_id);
        if (itr == bp.holding_threads.end()) return BreakpointType::SpinLock;

        return bp.kind;
    }
    return std::nullopt;   
}

bool is_breakpoint_type_step(const uintptr_t address, const DWORD thread_id) {
    if (!address_to_bp.contains(address)) return false;

    auto& bp = address_to_bp[address];

    // If thread doesn't hold the breakpoint return false
    auto itr = std::find(bp.holding_threads.begin(), bp.holding_threads.end(), thread_id);
    if (itr == bp.holding_threads.end()) return false;

    return bp.kind == BreakpointType::Step;
}

void print_all_user_breakpoints() {
    for (auto& [address, bp] : user_breakpoints) {
        std::print("[active={}; address={:p};  line={};  file={}; condition={}]\n", bp.active, reinterpret_cast<void *>(address), bp.line_number, bp.file_name, bp.condition);
    }
}

void print_user_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (user_breakpoints.contains(true_address)) {
        auto& bp = user_breakpoints[true_address];
        std::print("[active={}; address={:p};  line={};  file={}; condition={}]\n", bp.active, reinterpret_cast<void *>(address), bp.line_number, bp.file_name, bp.condition);
    }
}

bool is_user_breakpoint_conditional(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!user_breakpoints.contains(true_address)) return false;

    return !user_breakpoints[true_address].condition.empty();
}

std::optional<UserBreakpoint> get_user_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!user_breakpoints.contains(true_address)) return std::nullopt;

    return user_breakpoints[true_address];
}


bool disable_user_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!user_breakpoints.contains(true_address)) return false;

    auto& bp = user_breakpoints[true_address];
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.true_addr), &bp.saved_byte, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing original byte failed at address {:p} (original addr {:p}) with error {} when disabling breakpoint", bp.true_addr, bp.original_addr, GetLastError());
        return false;
    }
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.true_addr), 1);
    return true;
}

bool enable_user_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!user_breakpoints.contains(true_address)) return false;

    auto& bp = user_breakpoints[true_address];
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.true_addr), &int3, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing breakpoint byte failed at address {:p} (original addr {:p}) with error {} when re-enabling breakpoint", bp.true_addr, bp.original_addr, GetLastError());
        return false;
    }
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.true_addr), 1);
    return true;
}

bool disable_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!address_to_bp.contains(true_address)) return false;

    auto& bp = address_to_bp[true_address];
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.address), &bp.saved_byte, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing original byte failed at address {:p} with error {} when disabling breakpoint", bp.address, GetLastError());
        return false;
    }
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.address), 1);
    return true;
}

bool enable_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!address_to_bp.contains(true_address)) return false;

    auto& bp = address_to_bp[true_address];
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.address), &int3, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing breakpoint byte failed at address {:p} with error {} when re-enabling breakpoint", bp.address, GetLastError());
        return false;
    }
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.address), 1);
    return true;
}

bool remove_user_breakpoint(uintptr_t address) {
    uintptr_t true_address = get_true_address("cpp", address);
    if (!user_breakpoints.contains(true_address)) return false;

    auto& bp = user_breakpoints[true_address];
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.true_addr), &bp.saved_byte, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing original byte failed at address {:p} (original addr {:p}) with error {}", bp.true_addr, bp.original_addr, GetLastError());
        return false;
    }
    // Flush instruction cache so CPU sees the new byte
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.true_addr), 1);
    user_breakpoints.erase(true_address);
    return true;
}

}