#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>
#include <cstddef>
#include <cstdint>

// The PEB/TEB (and the structures they point at) that ship in <winternl.h> are
// almost entirely ReservedN[] padding, so nothing past ImageBaseAddress can be
// named. The layouts below are the full x64 ntdll layouts, which is what lets
// print_peb()/print_teb() produce windbg-style !peb / !teb output.
//
// They are prefixed FULL_ to avoid colliding with the winternl.h declarations
// of the same structures, which the rest of the codebase still uses.
//
// x64 only: every offset here assumes a 64-bit debuggee. The static_asserts at
// the bottom of this header pin the fields we actually read, so a bad layout is
// a compile error rather than garbage in the dump.
static_assert(sizeof(void *) == 8, "process_utils assumes an x64 PEB/TEB layout");

namespace chronoporia {

    struct FULL_CURDIR {
        UNICODE_STRING DosPath;
        HANDLE Handle;
    };

    // ntdll's ANSI STRING. <winternl.h> only declares the UNICODE_STRING form.
    struct FULL_STRING {
        USHORT Length;
        USHORT MaximumLength;
        PSTR Buffer;
    };

    struct FULL_RTL_DRIVE_LETTER_CURDIR {
        USHORT Flags;
        USHORT Length;
        ULONG TimeStamp;
        FULL_STRING DosPath;
    };

    struct FULL_RTL_USER_PROCESS_PARAMETERS {
        ULONG MaximumLength;                                    // 0x000
        ULONG Length;                                           // 0x004
        ULONG Flags;                                            // 0x008
        ULONG DebugFlags;                                       // 0x00c
        HANDLE ConsoleHandle;                                   // 0x010
        ULONG ConsoleFlags;                                     // 0x018
        HANDLE StandardInput;                                   // 0x020
        HANDLE StandardOutput;                                  // 0x028
        HANDLE StandardError;                                   // 0x030
        FULL_CURDIR CurrentDirectory;                           // 0x038
        UNICODE_STRING DllPath;                                 // 0x050
        UNICODE_STRING ImagePathName;                           // 0x060
        UNICODE_STRING CommandLine;                             // 0x070
        PVOID Environment;                                      // 0x080
        ULONG StartingX;                                        // 0x088
        ULONG StartingY;                                        // 0x08c
        ULONG CountX;                                           // 0x090
        ULONG CountY;                                           // 0x094
        ULONG CountCharsX;                                      // 0x098
        ULONG CountCharsY;                                      // 0x09c
        ULONG FillAttribute;                                    // 0x0a0
        ULONG WindowFlags;                                      // 0x0a4
        ULONG ShowWindowFlags;                                  // 0x0a8
        UNICODE_STRING WindowTitle;                             // 0x0b0
        UNICODE_STRING DesktopInfo;                             // 0x0c0
        UNICODE_STRING ShellInfo;                               // 0x0d0
        UNICODE_STRING RuntimeData;                             // 0x0e0
        FULL_RTL_DRIVE_LETTER_CURDIR CurrentDirectores[32];     // 0x0f0 (sic, ntdll's spelling)
        ULONG_PTR EnvironmentSize;                              // 0x3f0
        ULONG_PTR EnvironmentVersion;                           // 0x3f8
    };

    struct FULL_PEB_LDR_DATA {
        ULONG Length;                                           // 0x00
        BOOLEAN Initialized;                                    // 0x04
        HANDLE SsHandle;                                        // 0x08
        LIST_ENTRY InLoadOrderModuleList;                       // 0x10
        LIST_ENTRY InMemoryOrderModuleList;                     // 0x20
        LIST_ENTRY InInitializationOrderModuleList;             // 0x30
        PVOID EntryInProgress;                                  // 0x40
        BOOLEAN ShutdownInProgress;                             // 0x48
        HANDLE ShutdownThreadId;                                // 0x50
    };

    // Truncated after BaseDllName: everything print_peb() reports about a module
    // lives in the first 0x68 bytes, and the tail of this structure has churned
    // across Windows releases.
    struct FULL_LDR_DATA_TABLE_ENTRY {
        LIST_ENTRY InLoadOrderLinks;                            // 0x00
        LIST_ENTRY InMemoryOrderLinks;                          // 0x10
        LIST_ENTRY InInitializationOrderLinks;                  // 0x20
        PVOID DllBase;                                          // 0x30
        PVOID EntryPoint;                                       // 0x38
        ULONG SizeOfImage;                                      // 0x40
        UNICODE_STRING FullDllName;                             // 0x48
        UNICODE_STRING BaseDllName;                             // 0x58
    };

    // Truncated after SessionId; nothing past it is reported.
    struct FULL_PEB {
        BOOLEAN InheritedAddressSpace;                          // 0x000
        BOOLEAN ReadImageFileExecOptions;                       // 0x001
        BOOLEAN BeingDebugged;                                  // 0x002
        UCHAR BitField;                                         // 0x003
        HANDLE Mutant;                                          // 0x008
        PVOID ImageBaseAddress;                                 // 0x010
        PVOID Ldr;                                              // 0x018 FULL_PEB_LDR_DATA *
        PVOID ProcessParameters;                                // 0x020 FULL_RTL_USER_PROCESS_PARAMETERS *
        PVOID SubSystemData;                                    // 0x028
        PVOID ProcessHeap;                                      // 0x030
        PVOID FastPebLock;                                      // 0x038
        PVOID AtlThunkSListPtr;                                 // 0x040
        PVOID IFEOKey;                                          // 0x048
        ULONG CrossProcessFlags;                                // 0x050
        PVOID KernelCallbackTable;                              // 0x058
        ULONG SystemReserved;                                   // 0x060
        ULONG AtlThunkSListPtr32;                               // 0x064
        PVOID ApiSetMap;                                        // 0x068
        ULONG TlsExpansionCounter;                              // 0x070
        PVOID TlsBitmap;                                        // 0x078
        ULONG TlsBitmapBits[2];                                 // 0x080
        PVOID ReadOnlySharedMemoryBase;                         // 0x088
        PVOID SharedData;                                       // 0x090
        PVOID ReadOnlyStaticServerData;                         // 0x098
        PVOID AnsiCodePageData;                                 // 0x0a0
        PVOID OemCodePageData;                                  // 0x0a8
        PVOID UnicodeCaseTableData;                             // 0x0b0
        ULONG NumberOfProcessors;                               // 0x0b8
        ULONG NtGlobalFlag;                                     // 0x0bc
        LARGE_INTEGER CriticalSectionTimeout;                   // 0x0c0
        ULONG_PTR HeapSegmentReserve;                           // 0x0c8
        ULONG_PTR HeapSegmentCommit;                            // 0x0d0
        ULONG_PTR HeapDeCommitTotalFreeThreshold;               // 0x0d8
        ULONG_PTR HeapDeCommitFreeBlockThreshold;               // 0x0e0
        ULONG NumberOfHeaps;                                    // 0x0e8
        ULONG MaximumNumberOfHeaps;                             // 0x0ec
        PVOID ProcessHeaps;                                     // 0x0f0
        PVOID GdiSharedHandleTable;                             // 0x0f8
        PVOID ProcessStarterHelper;                             // 0x100
        ULONG GdiDCAttributeList;                               // 0x108
        PVOID LoaderLock;                                       // 0x110
        ULONG OSMajorVersion;                                   // 0x118
        ULONG OSMinorVersion;                                   // 0x11c
        USHORT OSBuildNumber;                                   // 0x120
        USHORT OSCSDVersion;                                    // 0x122
        ULONG OSPlatformId;                                     // 0x124
        ULONG ImageSubsystem;                                   // 0x128
        ULONG ImageSubsystemMajorVersion;                       // 0x12c
        ULONG ImageSubsystemMinorVersion;                       // 0x130
        ULONG_PTR ActiveProcessAffinityMask;                    // 0x138
        ULONG GdiHandleBuffer[60];                              // 0x140
        PVOID PostProcessInitRoutine;                           // 0x230
        PVOID TlsExpansionBitmap;                               // 0x238
        ULONG TlsExpansionBitmapBits[32];                       // 0x240
        ULONG SessionId;                                        // 0x2c0
    };

    struct FULL_ACTIVATION_CONTEXT_STACK {
        PVOID ActiveFrame;
        LIST_ENTRY FrameListCache;
        ULONG Flags;
        ULONG NextCookieSequenceNumber;
        ULONG StackId;
    };

    struct FULL_GDI_TEB_BATCH {
        ULONG Offset;
        ULONG_PTR HDC;
        ULONG Buffer[310];
    };

    // Truncated after HardErrorMode; nothing past it is reported. The large
    // Reserved/gl* arrays in the middle exist only to carry the offsets of the
    // fields that follow them (LastStatusValue, TlsSlots, HardErrorMode).
    struct FULL_TEB {
        NT_TIB NtTib;                                           // 0x0000
        PVOID EnvironmentPointer;                               // 0x0038
        PVOID UniqueProcess;                                    // 0x0040 ClientId.UniqueProcess
        PVOID UniqueThread;                                     // 0x0048 ClientId.UniqueThread
        PVOID ActiveRpcHandle;                                  // 0x0050
        PVOID ThreadLocalStoragePointer;                        // 0x0058
        PVOID ProcessEnvironmentBlock;                          // 0x0060
        ULONG LastErrorValue;                                   // 0x0068
        ULONG CountOfOwnedCriticalSections;                     // 0x006c
        PVOID CsrClientThread;                                  // 0x0070
        PVOID Win32ThreadInfo;                                  // 0x0078
        ULONG User32Reserved[26];                               // 0x0080
        ULONG UserReserved[5];                                  // 0x00e8
        PVOID WOW32Reserved;                                    // 0x0100
        ULONG CurrentLocale;                                    // 0x0108
        ULONG FpSoftwareStatusRegister;                         // 0x010c
        PVOID ReservedForDebuggerInstrumentation[16];           // 0x0110
        PVOID SystemReserved1[30];                              // 0x0190
        CHAR PlaceholderCompatibilityMode;                      // 0x0280
        BOOLEAN PlaceholderHydrationAlwaysExplicit;             // 0x0281
        CHAR PlaceholderReserved[10];                           // 0x0282
        ULONG ProxiedProcessId;                                 // 0x028c
        FULL_ACTIVATION_CONTEXT_STACK ActivationStack;          // 0x0290
        UCHAR WorkingOnBehalfTicket[8];                         // 0x02b8
        LONG ExceptionCode;                                     // 0x02c0
        PVOID ActivationContextStackPointer;                    // 0x02c8
        ULONG_PTR InstrumentationCallbackSp;                    // 0x02d0
        ULONG_PTR InstrumentationCallbackPreviousPc;            // 0x02d8
        ULONG_PTR InstrumentationCallbackPreviousSp;            // 0x02e0
        ULONG TxFsContext;                                      // 0x02e8
        BOOLEAN InstrumentationCallbackDisabled;                // 0x02ec
        BOOLEAN UnalignedLoadStoreExceptions;                   // 0x02ed
        FULL_GDI_TEB_BATCH GdiTebBatch;                         // 0x02f0
        PVOID RealUniqueProcess;                                // 0x07d8 RealClientId.UniqueProcess
        PVOID RealUniqueThread;                                 // 0x07e0 RealClientId.UniqueThread
        PVOID GdiCachedProcessHandle;                           // 0x07e8
        ULONG GdiClientPID;                                     // 0x07f0
        ULONG GdiClientTID;                                     // 0x07f4
        PVOID GdiThreadLocalInfo;                               // 0x07f8
        ULONG_PTR Win32ClientInfo[62];                          // 0x0800
        PVOID glDispatchTable[233];                             // 0x09f0
        ULONG_PTR glReserved1[29];                              // 0x1138
        PVOID glReserved2;                                      // 0x1220
        PVOID glSectionInfo;                                    // 0x1228
        PVOID glSection;                                        // 0x1230
        PVOID glTable;                                          // 0x1238
        PVOID glCurrentRC;                                      // 0x1240
        PVOID glContext;                                        // 0x1248
        LONG LastStatusValue;                                   // 0x1250
        UNICODE_STRING StaticUnicodeString;                     // 0x1258
        WCHAR StaticUnicodeBuffer[261];                         // 0x1268
        PVOID DeallocationStack;                                // 0x1478
        PVOID TlsSlots[64];                                     // 0x1480
        LIST_ENTRY TlsLinks;                                    // 0x1680
        PVOID Vdm;                                              // 0x1690
        PVOID ReservedForNtRpc;                                 // 0x1698
        PVOID DbgSsReserved[2];                                 // 0x16a0
        ULONG HardErrorMode;                                    // 0x16b0
    };

    // Pin the fields that are actually dereferenced or printed. If a layout
    // assumption above is wrong these fire at compile time.
    static_assert(offsetof(FULL_PEB, ImageBaseAddress) == 0x010);
    static_assert(offsetof(FULL_PEB, Ldr) == 0x018);
    static_assert(offsetof(FULL_PEB, ProcessParameters) == 0x020);
    static_assert(offsetof(FULL_PEB, ProcessHeap) == 0x030);
    static_assert(offsetof(FULL_PEB, NumberOfProcessors) == 0x0b8);
    static_assert(offsetof(FULL_PEB, NtGlobalFlag) == 0x0bc);
    static_assert(offsetof(FULL_PEB, PostProcessInitRoutine) == 0x230);
    static_assert(offsetof(FULL_PEB, SessionId) == 0x2c0);

    static_assert(offsetof(FULL_PEB_LDR_DATA, InLoadOrderModuleList) == 0x10);
    static_assert(offsetof(FULL_PEB_LDR_DATA, InInitializationOrderModuleList) == 0x30);

    static_assert(offsetof(FULL_LDR_DATA_TABLE_ENTRY, DllBase) == 0x30);
    static_assert(offsetof(FULL_LDR_DATA_TABLE_ENTRY, FullDllName) == 0x48);

    static_assert(offsetof(FULL_RTL_USER_PROCESS_PARAMETERS, CurrentDirectory) == 0x038);
    static_assert(offsetof(FULL_RTL_USER_PROCESS_PARAMETERS, ImagePathName) == 0x060);
    static_assert(offsetof(FULL_RTL_USER_PROCESS_PARAMETERS, CommandLine) == 0x070);
    static_assert(offsetof(FULL_RTL_USER_PROCESS_PARAMETERS, Environment) == 0x080);
    static_assert(offsetof(FULL_RTL_USER_PROCESS_PARAMETERS, WindowTitle) == 0x0b0);
    static_assert(offsetof(FULL_RTL_USER_PROCESS_PARAMETERS, CurrentDirectores) == 0x0f0);

    static_assert(offsetof(FULL_TEB, UniqueProcess) == 0x0040);
    static_assert(offsetof(FULL_TEB, ProcessEnvironmentBlock) == 0x0060);
    static_assert(offsetof(FULL_TEB, GdiTebBatch) == 0x02f0);
    static_assert(offsetof(FULL_TEB, LastStatusValue) == 0x1250);
    static_assert(offsetof(FULL_TEB, DeallocationStack) == 0x1478);
    // Cross-check against the winternl.h TEB, which the TLS code already indexes.
    static_assert(offsetof(FULL_TEB, TlsSlots) == offsetof(TEB, TlsSlots));
    static_assert(offsetof(FULL_TEB, HardErrorMode) == 0x16b0);

    // Read the debuggee's PEB and print it in the format of windbg's !peb,
    // including the loaded module list walked from PEB->Ldr.
    void print_peb();

    // Read the TEB of thread_id and print it in the format of windbg's !teb.
    // A thread_id of 0 prints the TEB of every thread in the debuggee.
    void print_teb(const DWORD thread_id);

    void print_handle_information(const HANDLE handle_offset);

    void populate_object_type_indexes();

    void print_tls_information(const int64_t tls_index, const DWORD thread_id);
}
