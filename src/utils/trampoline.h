#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Zydis/Zydis.h>
#include <array>
#include "globals.h"


namespace chronoporia {

// A 32 byte aligned region for instructions
// max instruction size is 15 bytes + 14 bytes for JMP gives 29
using InstructionRegion = std::array<uint8_t, 32>;

// 4096 bytes gives us 128 32 byte regions which should be plenty for now
void CreateTrampolineRegion(uint64_t region_size=4096);

// Create a Trampoline call for the first instruction at address
// Returns the address where the trampolined call lives
uintptr_t CreateTrampoline(const uintptr_t address);

// After all trampolines are created call this to commit them into memory
void FinalizeTrampolineRegion();

// Cleanup memory allocated to the trampoline when we no longer need it
void DestroyTrampolineRegion();

// Get the trampoline address for a specified address
uintptr_t GetTrampolineAddress(const uintptr_t address);
CONTEXT RedirectToTrampoline(const uintptr_t address, DWORD thread_id);

}