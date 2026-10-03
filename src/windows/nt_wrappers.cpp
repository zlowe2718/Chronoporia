#include "nt_wrappers.h"
#include "globals.h"
#include "quill/LogMacros.h"
#include <stdio.h>

namespace chronoporia {

PfnNtSuspendProcess          NtSuspendProcess          = nullptr;
PfnNtResumeProcess           NtResumeProcess           = nullptr;
PfnNtQueryInformationProcess NtQueryInformationProcess = nullptr;
PfnNtUnmapViewOfSection      NtUnmapViewOfSection      = nullptr;
PfnLdrLoadDll                LdrLoadDll                = nullptr;
PfnLdrUnloadDll              LdrUnloadDll              = nullptr;
PfnNtCreateThreadEx          NtCreateThreadEx          = nullptr;

void *                       NtAllocateVirtualMemory   = nullptr;
void *                       NtFreeVirtualMemory       = nullptr;
void *                       NtProtectVirtualMemory    = nullptr;
void *                       NtCreateThread            = nullptr;
void *                       NtTerminateThread         = nullptr;


void InitializeWrapperAddresses() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll");
    NtSuspendProcess          = (PfnNtSuspendProcess)         GetProcAddress(ntdll, "NtSuspendProcess");
    NtResumeProcess           = (PfnNtResumeProcess)          GetProcAddress(ntdll, "NtResumeProcess");
    NtQueryInformationProcess = (PfnNtQueryInformationProcess)GetProcAddress(ntdll, "NtQueryInformationProcess");
    NtUnmapViewOfSection      = (PfnNtUnmapViewOfSection)     GetProcAddress(ntdll, "NtUnmapViewOfSection");
    LdrLoadDll                = (PfnLdrLoadDll)               GetProcAddress(ntdll, "LdrLoadDll");
    LdrUnloadDll              = (PfnLdrUnloadDll)             GetProcAddress(ntdll, "LdrUnloadDll");
    NtCreateThreadEx          = (PfnNtCreateThreadEx)         GetProcAddress(ntdll, "NtCreateThreadEx");
    
    NtAllocateVirtualMemory = reinterpret_cast<void *>(GetProcAddress(ntdll, "NtAllocateVirtualMemory"));
    NtFreeVirtualMemory = reinterpret_cast<void *>(GetProcAddress(ntdll, "NtFreeVirtualMemory"));
    NtProtectVirtualMemory = reinterpret_cast<void *>(GetProcAddress(ntdll, "NtProtectVirtualMemory"));
    NtCreateThread = reinterpret_cast<void *>(GetProcAddress(ntdll, "NtCreateThread"));
    NtTerminateThread = reinterpret_cast<void *>(GetProcAddress(ntdll, "NtTerminateThread"));
}   

LONG SuspendProcess() {
    LONG error = NtSuspendProcess(globals::process_handle);
    if (error) {
        LOG_WARNING(globals::logger,"Suspend Process Failed with error code: {}", GetLastError());
    }
    return error;
}

LONG ResumeProcess() {
    LONG error = NtResumeProcess(globals::process_handle);
    if (error) {
        LOG_WARNING(globals::logger,"Resume Process Failed with error code: {}", GetLastError());
    }
    return error;
}

LONG QueryInformationProcess(const PROCESSINFOCLASS process_info_class, const PROCESS_BASIC_INFORMATION& pbi) {
    LONG error = NtQueryInformationProcess(globals::process_handle, process_info_class, const_cast<void *>(static_cast<const void *>(&pbi)), sizeof(pbi), nullptr);
    if (error) {
        LOG_WARNING(globals::logger,"NtQueryInformation Process Failed with error code: {}", GetLastError());
    };
    return error;
}

uintptr_t GetChildEntryAddress() {
    PROCESS_BASIC_INFORMATION pbi;
    NtQueryInformationProcess(globals::process_handle, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr);

    PEB peb;
    ReadProcessMemory(globals::process_handle, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr);
    PVOID image_base = peb.Reserved3[1]; 

    IMAGE_DOS_HEADER dos_header;
    IMAGE_NT_HEADERS nt_headers;
    
    ReadProcessMemory(globals::process_handle, image_base, &dos_header, sizeof(dos_header), nullptr);

    LPCVOID nt_header_addr = reinterpret_cast<LPCVOID>(reinterpret_cast<uintptr_t>(image_base) + dos_header.e_lfanew);
    ReadProcessMemory(globals::process_handle, nt_header_addr, &nt_headers, sizeof(nt_headers), nullptr);

    // entry point = image base + AddressOfEntryPoint
    return reinterpret_cast<uintptr_t>(image_base) + nt_headers.OptionalHeader.AddressOfEntryPoint;
}

}