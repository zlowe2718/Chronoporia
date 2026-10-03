#include "test_unload_dll.h"
#include <cstdint>
#include <libloaderapi.h>
#include <vector>

std::vector<std::byte> SetupDllUnloadShellCode(const std::vector<HMODULE>& dlls_to_unload) {
    ShellcodeParams params;
    std::vector<std::byte> shellcode_buffer;
    memset(&params, 0, sizeof(params));

    params.pfnFreeLibrary = FreeLibrary;

    for (uint64_t i = 0; i < dlls_to_unload.size(); i++) {
        params.entries[i] = {dlls_to_unload[i]};
    }
    shellcode_buffer.resize(sizeof(ShellcodeParams));
    memcpy(shellcode_buffer.data(), &params, sizeof(ShellcodeParams));
    return shellcode_buffer;
}

void InjectAndExecuteShellcode(const std::vector<std::byte>& shellcode_buffer) {
    HANDLE process = GetCurrentProcess();
    size_t code_size = reinterpret_cast<size_t>(shellcode_end) - reinterpret_cast<size_t>(_start);
    size_t params_size = sizeof(ShellcodeParams);
    size_t total_size = code_size + params_size;

    void *shellcode_addr = VirtualAlloc(nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    void *offset_addr = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(shellcode_addr) + params_size);
    WriteProcessMemory(process, shellcode_addr, shellcode_buffer.data(), params_size, nullptr);
    WriteProcessMemory(process, offset_addr, _start, code_size, nullptr);

    HANDLE thread = CreateRemoteThread(
        process,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(offset_addr),
        nullptr,
        0,
        nullptr
    );

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    VirtualFreeEx(process, shellcode_addr, 0, MEM_RELEASE);    
}

int main() {

    HMODULE dll_1 = LoadLibrary(L"dummy_dll_1.dll");
    LoadLibrary(L"dummy_dll_1.dll");
    LoadLibrary(L"dummy_dll_1.dll");
    HMODULE dll_2 = LoadLibrary(L"dummy_dll_2.dll");

    if (dll_1 != nullptr && dll_2 != nullptr) {
        printf("Dlls successfully loaded\n");
    } else {
        printf("Dlls failed to load\n");
    }

    std::vector<HMODULE> dlls_to_unload {dll_1, dll_2};
    auto shellcode_buffer = SetupDllUnloadShellCode(dlls_to_unload);
    InjectAndExecuteShellcode(shellcode_buffer);
    dll_1 = GetModuleHandle(L"dummy_dll_1.dll");
    dll_2 = GetModuleHandle(L"dummy_dll_2.dll");
    if (dll_1 == nullptr && dll_2 == nullptr) {
        printf("Dlls successfully unloaded\n");
    } else {
        printf("Dlls failed to unload\n");
    }
}

