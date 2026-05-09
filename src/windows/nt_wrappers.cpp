#include "nt_wrappers.h"
#include "globals.h"
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
        printf("\n\nSuspend Process Failed with error code: %ld\n\n", GetLastError());
    }
    return error;
}

LONG ResumeProcess() {
    LONG error = NtResumeProcess(globals::process_handle);
    if (error) {
        printf("\n\nResume Process Failed with error code: %ld\n\n", GetLastError());
    }
    return error;
}

LONG QueryInformationProcess(const PROCESSINFOCLASS process_info_class, const PROCESS_BASIC_INFORMATION& pbi) {
    LONG error = NtQueryInformationProcess(globals::process_handle, process_info_class, const_cast<void *>(static_cast<const void *>(&pbi)), sizeof(pbi), NULL);
    if (error) {
        printf("\n\nNtQueryInformation Process Failed with error code: %ld\n\n", GetLastError());
    };
    return error;
}

LONG UnmapViewOfSection() {
    return 0;
}

uintptr_t GetChildEntryAddress() {
    // 3. Get PEB address from ProcessBasicInformation
    PROCESS_BASIC_INFORMATION pbi;
    ULONG retLen;
    NtQueryInformationProcess(globals::process_handle, ProcessBasicInformation, &pbi, sizeof(pbi), &retLen);

    // 4. Read ImageBaseAddress from PEB (offset varies by architecture, usually Reserved3[1])
    PEB peb;
    ReadProcessMemory(globals::process_handle, pbi.PebBaseAddress, &peb, sizeof(peb), NULL);
    // TODO: Update the PEB to be fully typed per windows version
    PVOID imageBase = peb.Reserved3[1]; 

    // 5. Read PE Headers
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS ntHeaders;
    
    ReadProcessMemory(globals::process_handle, imageBase, &dosHeader, sizeof(dosHeader), NULL);

    LPCVOID nt_header_addr = reinterpret_cast<LPCVOID>(reinterpret_cast<uintptr_t>(imageBase) + dosHeader.e_lfanew);
    ReadProcessMemory(globals::process_handle, nt_header_addr, &ntHeaders, sizeof(ntHeaders), NULL);

    // 6. Calculate Entry Point: Base + AddressOfEntryPoint
    return reinterpret_cast<uintptr_t>(imageBase) + ntHeaders.OptionalHeader.AddressOfEntryPoint;
}

}