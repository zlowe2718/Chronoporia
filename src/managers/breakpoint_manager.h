#pragma once
#include <cstdint>
#include <optional>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>

namespace chronoporia {

constexpr std::byte int3 {0xCC};

// If the return breakpoint isn't a returning thread then spin lock the thread by rolling it back and letting it replay
enum class BreakpointType { Permanent, Return, Entry, ShellCode, SpinLock, User, Reverse, Step };

struct Breakpoint {
    BYTE           saved_byte;
    BreakpointType kind;
    size_t         count;
    uintptr_t      address;
    std::vector<DWORD> holding_threads;
};

struct UserBreakpoint {
    BYTE saved_byte;
    bool active;
    uintptr_t original_addr;
    uintptr_t true_addr;
    uint64_t line_number;
    uint64_t length;
    std::string file_name;
    std::string condition;
};

void CreateUserBreakpoint(uintptr_t address, const std::string& condition);
void CreatePermanentBreakpoint(uintptr_t address);
void CreateReturnBreakpoint(uintptr_t address, const DWORD thread_id);
void create_reverse_breakpoint(uintptr_t address, const DWORD thread_id);\
void create_step_breakpoint(uintptr_t address, const DWORD thread_id);

// Track the breakpoint at adress without writing it to the process if its already been written 
void TrackShellCodeBreakpoint(uintptr_t address);

bool RemoveBreakpoint(uintptr_t address, const DWORD thread_id);
bool RemovePermanentBreakpoint(uintptr_t address);
bool RemoveAllPermanentBreakpoints();

void WriteBreakpoint(uintptr_t address, BreakpointType bp_type);
bool RestoreMemory(const Breakpoint& bp);

std::optional<BreakpointType> GetBreakpointType(uintptr_t address, const DWORD thread_id);
bool is_breakpoint_type_step(const uintptr_t address, const DWORD thread_id);

void print_all_user_breakpoints();
void print_user_breakpoint(uintptr_t address);
bool is_user_breakpoint_conditional(uintptr_t address);
std::optional<UserBreakpoint> get_user_breakpoint(uintptr_t address);
bool remove_user_breakpoint(uintptr_t address);

// Temporarily disable the user breakpoint.  Writes the original byte but does not untrack the breakpoint
bool disable_user_breakpoint(uintptr_t address);
// Re-enable a disabled breakpoint
bool enable_user_breakpoint(uintptr_t address);

// Temporarily disable a breakpoint.  Writes the original byte but does not untrack the breakpoint
bool disable_breakpoint(uintptr_t address);
// Re-enable a disabled breakpoint
bool enable_breakpoint(uintptr_t address);
}