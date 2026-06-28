#include "shellcode.h"
#include "globals.h"
#include "nt_wrappers.h"
#include "quill/LogMacros.h"
#include <cstdint>
#include <libloaderapi.h>
#include <vector>
#include <array>
#include <iterator>

namespace {
    std::vector<uint8_t> shellcode_buffer; 
}

namespace chronoporia {

    uintptr_t WriteShellCodeBufferToProcess() {
        uint64_t total_code_size = shellcode_buffer.size();
        void *remote_code_address = VirtualAllocEx(
            globals::process_handle, NULL, total_code_size,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
        );
        WriteProcessMemory(globals::process_handle, remote_code_address, shellcode_buffer.data(), total_code_size, nullptr);
        FlushInstructionCache(globals::process_handle, remote_code_address, total_code_size);
        shellcode_buffer.clear();
        // TODO: change this to be more dynamic in case we have different shellcodes to run
        return reinterpret_cast<uintptr_t>(remote_code_address); //+ sizeof(ShellcodeParams);
    }

    void FreeShellCodeAtAddress(uintptr_t shell_address) {
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQueryEx(globals::process_handle, reinterpret_cast<void *>(shell_address), &mbi, sizeof(mbi));

        if (!VirtualFreeEx(globals::process_handle, reinterpret_cast<void *>(shell_address), 0, MEM_RELEASE))
        {
            LOG_WARNING(globals::logger, "VirtualFreeEx (shellcode) failed:\n"
                "    error:    {}\n"
                "    address:  {:p}", GetLastError(), shell_address);
        }
        FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(shell_address), mbi.RegionSize);    
    }

    // Dll Unload shellcode:
    //   for each DLL:
    //     mov rcx, <dll_base>    ; 48 B9 <8 bytes>
    //     mov rax, <LdrUnloadDll> ; 48 B8 <8 bytes>
    //     call rax               ; FF D0
    //   int3                     ; CC
    // Add the int3 at the end so we can track when its finished and unhijack the process thread
    uint64_t AddDllUnload(const std::vector<DllInfo>& dlls_to_unload) {
        std::vector<uint8_t> code_line;

        std::array<uint8_t, 4> sub_rsp = {0x48, 0x83, 0xEC, 0x28};
        std::array<uint8_t, 4> add_rsp = {0x48, 0x83, 0xC4, 0x28};
        // 8 extra bytes to fix the rsp register + 1 byte for the breakpoint
        code_line.resize(dlls_to_unload.size() * 22 + 8 + 1);

        memcpy(code_line.data(), sub_rsp.data(), 4);

        std::array<uint8_t, 22> base_dll_unload_code {
            0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, <dll_base>
            0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, <LdrUnloadDll>
            0xFF, 0xD0                                                  // call rax
        };
        void *unload_ptr = reinterpret_cast<void *>(LdrUnloadDll);
        memcpy(base_dll_unload_code.data() + 12, &unload_ptr, 8);

        uint64_t dll_count = 0;
        // HMODULE is just a pointer to the beginning of the dll
        for (const auto& dll_info : dlls_to_unload) {
            memcpy(code_line.data() + dll_count * 22 + 4, base_dll_unload_code.data(), 22);
            memcpy(code_line.data() + dll_count * 22 + 2 + 4, &dll_info.dll_handle, 8);
            dll_count += 1;
        }

        memcpy(code_line.data() + dll_count * 22 + 4, add_rsp.data(), 4);
        code_line.back() = 0xCC;

        uint64_t total_code_size = code_line.size();
        shellcode_buffer.reserve(shellcode_buffer.size() + total_code_size);
        std::move(code_line.begin(), code_line.end(), std::back_inserter(shellcode_buffer));
        return total_code_size;
    }


    uint64_t SetupDllUnloadShellCode(const std::vector<DllInfo>& dlls_to_unload, uint64_t start_idx) {
        ShellcodeParams params;
        memset(&params, 0, sizeof(params));

        params.pfnFreeLibrary = FreeLibrary;

        for (uint64_t i = start_idx; i < dlls_to_unload.size(); i++) {
            params.entries[i] = {dlls_to_unload[i].dll_handle};
        }

        size_t code_size = reinterpret_cast<size_t>(shellcode_end) - reinterpret_cast<size_t>(_start);
        size_t params_size = sizeof(ShellcodeParams);
        size_t total_size = code_size + params_size;

        // Load in the data then the asm shellcode
        shellcode_buffer.resize(total_size);
        memcpy(shellcode_buffer.data(), &params, sizeof(ShellcodeParams));
        memcpy(shellcode_buffer.data() + params_size, _start, code_size);
        return total_size;
    }
}