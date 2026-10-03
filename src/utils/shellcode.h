#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include "module_manager.h"

extern "C" void _start();
extern "C" void shellcode_end(); // asm end label to compute size

namespace chronoporia {
    #pragma pack(push, 1)
    struct UnloadEntry {
        HMODULE hModule;
    };
    struct ShellcodeParams {
        void *pfnFreeLibrary;
        UnloadEntry entries[8];
    };
    #pragma pack(pop)


    uintptr_t WriteShellCodeBufferToProcess();
    void FreeShellCodeAtAddress(uintptr_t shell_address);

    uint64_t AddDllUnload(const std::vector<DllInfo>& dlls_to_unload);
    uint64_t SetupDllUnloadShellCode(const std::vector<DllInfo>& dlls_to_unload, uint64_t start_idx = 0);
}