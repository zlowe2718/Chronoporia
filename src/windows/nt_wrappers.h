#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>
#include "nt_undocumented.h"

namespace chronoporia {

typedef LONG (NTAPI *PfnNtSuspendProcess)(IN HANDLE ProcessHandle);
typedef LONG (NTAPI *PfnNtResumeProcess)(IN HANDLE ProcessHandle);
typedef LONG (NTAPI *PfnNtQueryInformationProcess)(
    IN  HANDLE                       ProcessHandle,
    IN  PROCESSINFOCLASS	         ProcessInformationClass,
    OUT PVOID                        ProcessInformation,
    IN  ULONG                        ProcessInformationLength,
    OUT PULONG                       ReturnLength
);
typedef LONG (NTAPI *PfnNtUnmapViewOfSection)(IN HANDLE ProcessHandle, IN PVOID BaseAddress);
typedef NTSTATUS (NTAPI *PfnLdrLoadDll)(
    IN PCWSTR DllPath, 
    IN PULONG DllCharacteristics, 
    IN PCUNICODE_STRING DllName, 
    OUT PVOID *DllHandle
);
typedef NTSTATUS (NTAPI *PfnLdrUnloadDll)(IN HMODULE DllHandle);
typedef NTSTATUS (NTAPI *PfnNtCreateThreadEx)(
    OUT PHANDLE thread_handle,
    IN ACCESS_MASK desired_access,
    IN OPTIONAL PCOBJECT_ATTRIBUTES object_attributes,
    IN HANDLE process_handle,
    IN PUSER_THREAD_START_ROUTINE start_routine,
    IN OPTIONAL PVOID argument,
    IN ULONG create_flags,
    IN SIZE_T zero_bits,
    IN SIZE_T stack_size,
    IN SIZE_T maximum_stack_size,
    IN OPTIONAL PPS_ATTRIBUTE_LIST attribute_list
);

extern PfnNtSuspendProcess          NtSuspendProcess;
extern PfnNtResumeProcess           NtResumeProcess;
extern PfnNtQueryInformationProcess NtQueryInformationProcess;
extern PfnNtUnmapViewOfSection      NtUnmapViewOfSection;
extern PfnLdrLoadDll                LdrLoadDll;
extern PfnLdrUnloadDll              LdrUnloadDll;
extern PfnNtCreateThreadEx          NtCreateThreadEx;

extern void *                       NtTerminateThread;
extern void *                       NtAllocateVirtualMemory;
extern void *                       NtFreeVirtualMemory;
extern void *                       NtProtectVirtualMemory;
extern void *                       NtCreateThread;

// Initialize all NtDLL function pointers
void InitializeWrapperAddresses();

// Suspend the target process
// Returns the NtStatus value (not the windows error code)
LONG SuspendProcess();

// Resume the target process
// Returns the NtStatus value (not the windows error code)
LONG ResumeProcess();

// Queries the target process to get the PROCESS_BASIC_INFORMATION data
// Not currently used
LONG QueryInformationProcess(const PROCESSINFOCLASS process_info_class, const PROCESS_BASIC_INFORMATION& pbi);

// Forcibly unmap a mapped view
// Returns the NtStatus value (not the windows error code) 
// TODO: Implement
LONG UnmapViewOfSection();

uintptr_t GetChildEntryAddress();

};