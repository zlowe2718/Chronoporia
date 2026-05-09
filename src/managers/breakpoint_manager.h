#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>

namespace chronoporia {

constexpr std::byte int3 {0xCC};

// If the return breakpoint isn't a returning thread then spin lock the thread by rolling it back and letting it replay
enum class BreakpointType { Permanent, Return, Entry, ShellCode, SpinLock };

struct Breakpoint {
    BYTE           savedByte;
    BreakpointType kind;
    size_t         count;
    uintptr_t      address;
    std::vector<DWORD> holding_threads;
};


void CreatePermanentBreakpoint(uintptr_t address);

void CreateReturnBreakpoint(uintptr_t address, const DWORD thread_id);

// Track the breakpoint at adress without writing it to the process if its already been written 
void TrackShellCodeBreakpoint(uintptr_t address);

bool RemoveBreakpoint(uintptr_t address, const DWORD thread_id);
bool RemovePermanentBreakpoint(uintptr_t address);
bool RemoveAllPermanentBreakpoints();

void WriteBreakpoint(uintptr_t address, BreakpointType bp_type);
bool RestoreMemory(const Breakpoint& bp);

BreakpointType GetBreakpointType(uintptr_t address, const DWORD thread_id);
}