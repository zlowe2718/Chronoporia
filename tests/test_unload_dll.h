#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>

extern "C" void _start();
extern "C" void shellcode_end(); // asm end label to compute size

#pragma pack(push, 1)
struct UnloadEntry {
    HMODULE hModule;
};
struct ShellcodeParams {
    void *pfnFreeLibrary;
    UnloadEntry entries[8];
};
#pragma pack(pop)

std::vector<std::byte> SetupDllUnloadShellCode(const std::vector<HMODULE>& dlls_to_unload);
void InjectAndExecuteShellcode(const std::vector<std::byte>& shellcode_buffer);