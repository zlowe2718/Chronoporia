#include "breakpoint_manager.h"
#include "globals.h"
#include "quill/LogMacros.h"
#include "trampoline.h"
#include <cstdint>
#include <unordered_map>

namespace {
    std::unordered_map<uintptr_t, chronoporia::Breakpoint> address_to_bp;
    std::unordered_map<uintptr_t, uintptr_t> permanent_bp_to_trampoline;
}

namespace chronoporia {

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
    if (!WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(bp.address), &bp.savedByte, 1, nullptr)) {
        LOG_WARNING(globals::logger, "writing original byte failed at address {:p} with error {}", bp.address, GetLastError());
        return false;
    }
    // Flush instruction cache so CPU sees the new byte
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(bp.address), 1);
    return true;
}

BreakpointType GetBreakpointType(uintptr_t address, const DWORD thread_id) {
    if (permanent_bp_to_trampoline.contains(address)) {
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
    return BreakpointType::SpinLock;   
}

}