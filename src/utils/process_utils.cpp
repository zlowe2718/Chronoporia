#include "process_utils.h"
#include "globals.h"
#include "misc.h"
#include "nt_wrappers.h"
#include "thread_utils.h"
#include "quill/LogMacros.h"
#include "nt_undocumented.h"
#include <TlHelp32.h>
#include <ctime>
#include <handleapi.h>
#include <optional>
#include <processthreadsapi.h>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <winnt.h>
#include <winternl.h>

namespace {
    using namespace chronoporia;

    // A UNICODE_STRING in the debuggee longer than this is treated as corrupt
    // rather than trusted for an allocation.
    constexpr USHORT kMaxUnicodeStringBytes = 8192;

    // Guard against a corrupt (or concurrently mutating) loader list looping forever.
    constexpr size_t kMaxModules = 512;

    std::unordered_map<USHORT, std::string> object_type_index_to_name;

    template <typename T>
    bool read_remote(const void *address, T& out) {
        if (address == nullptr) {
            return false;
        }
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(globals::process_handle, address, &out, sizeof(T), &bytes_read)
            || bytes_read != sizeof(T)) {
            return false;
        }
        return true;
    }

    template <typename T>
    bool read_remote(const uintptr_t address, T& out) {
        return read_remote(reinterpret_cast<const void *>(address), out);
    }

    // Follows a UNICODE_STRING read out of the debuggee to its buffer and narrows
    // it. Returns nullopt when the string is empty or the buffer is unreadable, so
    // callers can print windbg's "< Name not readable >" placeholder.
    std::optional<std::string> read_remote_unicode_string(const UNICODE_STRING& unicode_string) {
        if (unicode_string.Buffer == nullptr || unicode_string.Length == 0
            || unicode_string.Length > kMaxUnicodeStringBytes) {
            return std::nullopt;
        }

        std::wstring wide(unicode_string.Length / sizeof(wchar_t), L'\0');
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(globals::process_handle, unicode_string.Buffer, wide.data(),
                               unicode_string.Length, &bytes_read)
            || bytes_read != unicode_string.Length) {
            return std::nullopt;
        }

        return wstring_to_string(wide);
    }

    void print_remote_string(const char *label, const UNICODE_STRING& unicode_string) {
        const auto value = read_remote_unicode_string(unicode_string);
        if (value) {
            printf("    %-14s'%s'\n", label, value->c_str());
        } else {
            printf("    %-14s< Name not readable >\n", label);
        }
    }

    // Formats a PE TimeDateStamp the way windbg's module list does
    // (e.g. "Aug 24 09:41:07 2026"). Returns a placeholder for a zero or
    // unformattable stamp, which is normal for images built with /Brepro.
    std::string format_pe_timestamp(const ULONG time_date_stamp) {
        if (time_date_stamp == 0) {
            return "                    ";
        }

        const __time64_t stamp = static_cast<__time64_t>(time_date_stamp);
        tm broken_down {};
        if (_localtime64_s(&broken_down, &stamp) != 0) {
            return "                    ";
        }

        char buffer[32] {};
        if (strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S %Y", &broken_down) == 0) {
            return "                    ";
        }
        return buffer;
    }

    // Reads the PE header at a loaded module's base to recover its link timestamp.
    // Returns 0 if the headers can't be read or don't look like a PE image.
    ULONG read_module_timestamp(const void *dll_base) {
        IMAGE_DOS_HEADER dos_header;
        if (!read_remote(dll_base, dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE) {
            return 0;
        }

        IMAGE_NT_HEADERS nt_headers;
        const uintptr_t nt_header_address = reinterpret_cast<uintptr_t>(dll_base) + dos_header.e_lfanew;
        if (!read_remote(nt_header_address, nt_headers) || nt_headers.Signature != IMAGE_NT_SIGNATURE) {
            return 0;
        }

        return nt_headers.FileHeader.TimeDateStamp;
    }

    // Decodes the NtGlobalFlag bits windbg names in its !peb output. Only the
    // flags a debugger session realistically sees are listed.
    void print_nt_global_flags(const ULONG nt_global_flag) {
        struct GlobalFlag {
            ULONG bit;
            const char *name;
        };

        static constexpr GlobalFlag flags[] = {
            {0x00000001, "FLG_STOP_ON_EXCEPTION"},
            {0x00000002, "FLG_SHOW_LDR_SNAPS"},
            {0x00000004, "FLG_DEBUG_INITIAL_COMMAND"},
            {0x00000008, "FLG_STOP_ON_HUNG_GUI"},
            {0x00000010, "FLG_HEAP_ENABLE_TAIL_CHECK"},
            {0x00000020, "FLG_HEAP_ENABLE_FREE_CHECK"},
            {0x00000040, "FLG_HEAP_VALIDATE_PARAMETERS"},
            {0x00000080, "FLG_HEAP_VALIDATE_ALL"},
            {0x00000100, "FLG_APPLICATION_VERIFIER"},
            {0x00000400, "FLG_HEAP_ENABLE_TAGGING"},
            {0x00000800, "FLG_USER_STACK_TRACE_DB"},
            {0x00001000, "FLG_KERNEL_STACK_TRACE_DB"},
            {0x00002000, "FLG_MAINTAIN_OBJECT_TYPELIST"},
            {0x00004000, "FLG_HEAP_ENABLE_TAG_BY_DLL"},
            {0x00008000, "FLG_DISABLE_STACK_EXTENSION"},
            {0x00010000, "FLG_ENABLE_CSRDEBUG"},
            {0x00020000, "FLG_ENABLE_KDEBUG_SYMBOL_LOAD"},
            {0x00040000, "FLG_DISABLE_PAGE_KERNEL_STACKS"},
            {0x00080000, "FLG_ENABLE_SYSTEM_CRIT_BREAKS"},
            {0x00100000, "FLG_HEAP_DISABLE_COALESCING"},
            {0x00200000, "FLG_ENABLE_CLOSE_EXCEPTIONS"},
            {0x00400000, "FLG_ENABLE_EXCEPTION_LOGGING"},
            {0x00800000, "FLG_ENABLE_HANDLE_TYPE_TAGGING"},
            {0x01000000, "FLG_HEAP_PAGE_ALLOCS"},
            {0x02000000, "FLG_DEBUG_INITIAL_COMMAND_EX"},
            {0x04000000, "FLG_DISABLE_DBGPRINT"},
            {0x08000000, "FLG_CRITSEC_EVENT_CREATION"},
            {0x40000000, "FLG_ENABLE_HANDLE_EXCEPTIONS"},
            {0x80000000, "FLG_DISABLE_PROTDLLS"},
        };

        for (const auto& [bit, name] : flags) {
            if (nt_global_flag & bit) {
                printf("        %s (%lx)\n", name, bit);
            }
        }
    }

    void print_module_list(const FULL_PEB_LDR_DATA& ldr, const uintptr_t ldr_address) {
        printf("            Base TimeStamp                     Module\n");

        // InLoadOrderLinks sits at offset 0 of LDR_DATA_TABLE_ENTRY, so a link
        // address in this list is also the entry address.
        const uintptr_t list_head = ldr_address + offsetof(FULL_PEB_LDR_DATA, InLoadOrderModuleList);
        uintptr_t current = reinterpret_cast<uintptr_t>(ldr.InLoadOrderModuleList.Flink);

        for (size_t module_count = 0; current != 0 && current != list_head; ++module_count) {
            if (module_count >= kMaxModules) {
                printf("        < module list truncated after %zu entries >\n", kMaxModules);
                return;
            }

            FULL_LDR_DATA_TABLE_ENTRY entry;
            if (!read_remote(current, entry)) {
                printf("        < module list entry at %p not readable >\n",
                       reinterpret_cast<void *>(current));
                return;
            }

            const auto full_name = read_remote_unicode_string(entry.FullDllName);
            printf("        %16llx %s %s\n",
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(entry.DllBase)),
                   format_pe_timestamp(read_module_timestamp(entry.DllBase)).c_str(),
                   full_name ? full_name->c_str() : "< Name not readable >");

            current = reinterpret_cast<uintptr_t>(entry.InLoadOrderLinks.Flink);
        }
    }

    void print_process_parameters(const void *process_parameters_address) {
        FULL_RTL_USER_PROCESS_PARAMETERS parameters;
        if (!read_remote(process_parameters_address, parameters)) {
            printf("    ProcessParameters: %16llx  < not readable >\n",
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(process_parameters_address)));
            return;
        }

        printf("    ProcessParameters: %16llx\n",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(process_parameters_address)));
        print_remote_string("CurrentDirectory:", parameters.CurrentDirectory.DosPath);
        print_remote_string("WindowTitle:", parameters.WindowTitle);
        print_remote_string("ImageFile:", parameters.ImagePathName);
        print_remote_string("CommandLine:", parameters.CommandLine);
        print_remote_string("DllPath:", parameters.DllPath);
        printf("    %-14s%16llx\n", "Environment:",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(parameters.Environment)));
        printf("    %-14s%16llx . %llx . %llx\n", "Std handles:",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(parameters.StandardInput)),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(parameters.StandardOutput)),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(parameters.StandardError)));
    }

    void print_teb_at(const DWORD thread_id, const uintptr_t teb_address) {
        FULL_TEB teb;
        if (!read_remote(teb_address, teb)) {
            printf("TEB for thread %lu at %016llx is not readable\n", thread_id,
                   static_cast<unsigned long long>(teb_address));
            return;
        }

        printf("TEB at %016llx (thread %lu)\n", static_cast<unsigned long long>(teb_address), thread_id);
        printf("    ExceptionList:        %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.ExceptionList));
        printf("    StackBase:            %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.StackBase));
        printf("    StackLimit:           %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.StackLimit));
        printf("    SubSystemTib:         %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.SubSystemTib));
        printf("    FiberData:            %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.FiberData));
        printf("    ArbitraryUserPointer: %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.ArbitraryUserPointer));
        printf("    Self:                 %016llx\n", reinterpret_cast<unsigned long long>(teb.NtTib.Self));
        printf("    EnvironmentPointer:   %016llx\n", reinterpret_cast<unsigned long long>(teb.EnvironmentPointer));
        printf("    ClientId:             %016llx . %016llx\n",
               reinterpret_cast<unsigned long long>(teb.UniqueProcess),
               reinterpret_cast<unsigned long long>(teb.UniqueThread));
        printf("    RpcHandle:            %016llx\n", reinterpret_cast<unsigned long long>(teb.ActiveRpcHandle));
        printf("    Tls Storage:          %016llx\n", reinterpret_cast<unsigned long long>(teb.ThreadLocalStoragePointer));
        printf("    PEB Address:          %016llx\n", reinterpret_cast<unsigned long long>(teb.ProcessEnvironmentBlock));
        printf("    LastErrorValue:       %lu\n", teb.LastErrorValue);
        printf("    LastStatusValue:      %lx\n", static_cast<unsigned long>(teb.LastStatusValue));
        printf("    Count Owned Locks:    %lu\n", teb.CountOfOwnedCriticalSections);
        printf("    HardErrorMode:        %lu\n", teb.HardErrorMode);
        printf("    DeallocationStack:    %016llx\n", reinterpret_cast<unsigned long long>(teb.DeallocationStack));
        printf("\n");
    }

    // Resolves a thread's TEB address without touching thread_utils' cached
    // handle map, so dumping a TEB never adds threads to the tracked set.
    uintptr_t query_teb_address(const DWORD thread_id) {
        const HANDLE thread_handle = OpenThread(THREAD_QUERY_INFORMATION, false, thread_id);
        if (thread_handle == nullptr) {
            LOG_WARNING(globals::logger, "Could not open thread {} to read its TEB, error {}",
                        thread_id, GetLastError());
            return 0;
        }

        const uintptr_t teb_address = get_teb_address(thread_handle);
        CloseHandle(thread_handle);
        return teb_address;
    }

    std::vector<DWORD> debuggee_thread_ids() {
        std::vector<DWORD> thread_ids;

        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, globals::process_id);
        if (snapshot == INVALID_HANDLE_VALUE) {
            LOG_WARNING(globals::logger, "Could not snapshot threads, error {}", GetLastError());
            return thread_ids;
        }

        THREADENTRY32 thread_entry {};
        thread_entry.dwSize = sizeof(thread_entry);
        if (Thread32First(snapshot, &thread_entry)) {
            do {
                if (thread_entry.th32OwnerProcessID == globals::process_id) {
                    thread_ids.push_back(thread_entry.th32ThreadID);
                }
            } while (Thread32Next(snapshot, &thread_entry));
        }

        CloseHandle(snapshot);
        return thread_ids;
    }

    uint64_t align_up(const USHORT size, size_t alignment) {
        return (static_cast<uint64_t>(size) + static_cast<uint64_t>(alignment) - 1) & ~(static_cast<uint64_t>(alignment) - 1);
    }

    void print_all_handles() {
        uint32_t size = 0x10000;
        std::vector<BYTE> buffer(0x10000);
        ULONG return_length = 0;

        NTSTATUS status = NtQuerySystemInformation(static_cast<::SYSTEM_INFORMATION_CLASS>(SystemExtendedHandleInformation), buffer.data(), size, &return_length);

        while (status == STATUS_INFO_LENGTH_MISMATCH) {
            size = return_length; // Grow buffer based on system requirements
            buffer.resize(size);
            status = NtQuerySystemInformation(static_cast<::SYSTEM_INFORMATION_CLASS>(SystemExtendedHandleInformation), buffer.data(), size, &return_length);
        }

        SYSTEM_HANDLE_INFORMATION_EX *handle_infos = reinterpret_cast<SYSTEM_HANDLE_INFORMATION_EX *>(buffer.data());
        for (uint32_t i = 0; i < handle_infos->NumberOfHandles; i++) {
            // Filter handles matching our target process ID
            const SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX &handle_info = handle_infos->Handles[i];
            if (reinterpret_cast<uintptr_t>(handle_info.UniqueProcessId) == globals::process_id) {
                const auto type_name = object_type_index_to_name[handle_info.ObjectTypeIndex];
                printf("Handle %llx:\n", reinterpret_cast<uint64_t>(handle_info.HandleValue));
                printf("  Type          %s\n", type_name.data());
            }
        }
    }

    void print_handle_info(const HANDLE handle_value) {
        HANDLE new_handle {};
        if(!DuplicateHandle(globals::process_handle, handle_value, GetCurrentProcess(), &new_handle, 0, false, DUPLICATE_SAME_ACCESS)) {
            LOG_ERROR(globals::logger, "Could not duplicate child handle for handle {:x}", handle_value);
            return;
        }

        ULONG return_length {};

        OBJECT_BASIC_INFORMATION handle_basic_info {};
        NtQueryObject(new_handle, ::ObjectBasicInformation, &handle_basic_info, sizeof(OBJECT_BASIC_INFORMATION), &return_length);

        // variable length structure since Windows injects the string buffer at the end of the struct
        uint64_t aligned_type_size = (handle_basic_info.TypeInfoSize + 7) & ~7;
        std::vector<std::byte> type_buffer(aligned_type_size);
        auto *handle_type_info = reinterpret_cast<OBJECT_TYPE_INFORMATION *>(type_buffer.data());
        NTSTATUS status = NtQueryObject(new_handle, ::ObjectTypeInformation, handle_type_info, aligned_type_size, &return_length);

        std::string handle_name {"<None>"};
        uint64_t aligned_name_size = (handle_basic_info.NameInfoSize + 7) & ~7;
        std::vector<std::byte> name_buffer(aligned_name_size);
        auto *handle_name_info = reinterpret_cast<OBJECT_NAME_INFORMATION *>(name_buffer.data());
        if (handle_basic_info.NameInfoSize > 0) {
            NtQueryObject(new_handle, static_cast<::OBJECT_INFORMATION_CLASS>(ObjectNameInformation), handle_name_info, aligned_name_size, &return_length);
            handle_name = wstring_to_string(std::wstring(handle_name_info->Name.Buffer, handle_name_info->Name.Length / sizeof(wchar_t)));
        }

        printf("Handle %llx:\n", reinterpret_cast<uint64_t>(handle_value));
        printf("  Type          %s\n", object_type_index_to_name[handle_type_info->TypeIndex].data());
        printf("  Attributes    %ld\n", handle_basic_info.Attributes);
        printf("  GrantedAccess 0x%lx\n", handle_basic_info.GrantedAccess);
        printf("  HandleCount   %ld\n", handle_basic_info.HandleCount);
        printf("  PointerCount  %ld\n", handle_basic_info.PointerCount);
        printf("  Name          %s\n", handle_name.data());

        CloseHandle(new_handle);
    }

    std::vector<uint64_t> get_indices_of_true_bits(ULONG bit_value, ULONG offset) {
        std::vector<uint64_t> indices {};

        uint64_t index = offset;
        while (bit_value > 0) {
            if (bit_value & 1) {
                indices.push_back(index);
            }
            bit_value >>= 1;
            index += 1;
        }

        return indices;
    }
}

namespace chronoporia {

    void print_peb() {
        PROCESS_BASIC_INFORMATION pbi {};
        if (!NT_SUCCESS(NtQueryInformationProcess(globals::process_handle, ProcessBasicInformation,
                                                  &pbi, sizeof(pbi), nullptr))) {
            printf("Could not query the process's PEB address (error %lu)\n", GetLastError());
            return;
        }

        FULL_PEB peb;
        if (!read_remote(pbi.PebBaseAddress, peb)) {
            printf("PEB at %016llx is not readable\n",
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pbi.PebBaseAddress)));
            return;
        }

        printf("PEB at %016llx\n",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pbi.PebBaseAddress)));
        printf("    InheritedAddressSpace:    %s\n", peb.InheritedAddressSpace ? "Yes" : "No");
        printf("    ReadImageFileExecOptions: %s\n", peb.ReadImageFileExecOptions ? "Yes" : "No");
        printf("    BeingDebugged:            %s\n", peb.BeingDebugged ? "Yes" : "No");
        printf("    ImageBaseAddress:         %016llx\n",
               reinterpret_cast<unsigned long long>(peb.ImageBaseAddress));
        printf("    NtGlobalFlag:             %lx\n", peb.NtGlobalFlag);
        print_nt_global_flags(peb.NtGlobalFlag);

        const uintptr_t ldr_address = reinterpret_cast<uintptr_t>(peb.Ldr);
        printf("    Ldr                       %016llx\n", static_cast<unsigned long long>(ldr_address));

        FULL_PEB_LDR_DATA ldr;
        if (read_remote(ldr_address, ldr)) {
            printf("    Ldr.Initialized:          %s\n", ldr.Initialized ? "Yes" : "No");
            printf("    Ldr.InInitializationOrderModuleList: %016llx . %016llx\n",
                   reinterpret_cast<unsigned long long>(ldr.InInitializationOrderModuleList.Flink),
                   reinterpret_cast<unsigned long long>(ldr.InInitializationOrderModuleList.Blink));
            printf("    Ldr.InLoadOrderModuleList:           %016llx . %016llx\n",
                   reinterpret_cast<unsigned long long>(ldr.InLoadOrderModuleList.Flink),
                   reinterpret_cast<unsigned long long>(ldr.InLoadOrderModuleList.Blink));
            printf("    Ldr.InMemoryOrderModuleList:         %016llx . %016llx\n",
                   reinterpret_cast<unsigned long long>(ldr.InMemoryOrderModuleList.Flink),
                   reinterpret_cast<unsigned long long>(ldr.InMemoryOrderModuleList.Blink));
            print_module_list(ldr, ldr_address);
        } else {
            printf("    Ldr.Initialized:          < loader data not readable >\n");
        }

        printf("    SubSystemData:     %16llx\n", reinterpret_cast<unsigned long long>(peb.SubSystemData));
        printf("    ProcessHeap:       %16llx\n", reinterpret_cast<unsigned long long>(peb.ProcessHeap));
        print_process_parameters(peb.ProcessParameters);

        printf("    NumberOfProcessors:  %lu\n", peb.NumberOfProcessors);
        printf("    NumberOfHeaps:       %lu (max %lu)\n", peb.NumberOfHeaps, peb.MaximumNumberOfHeaps);
        printf("    SessionId:           %lu\n", peb.SessionId);
        printf("    OS Version:          %lu.%lu build %u\n",
               peb.OSMajorVersion, peb.OSMinorVersion, peb.OSBuildNumber);
        printf("    ImageSubsystem:      %lu (%lu.%lu)\n",
               peb.ImageSubsystem, peb.ImageSubsystemMajorVersion, peb.ImageSubsystemMinorVersion);
        printf("\n");
    }

    void print_teb(const DWORD thread_id) {
        if (thread_id != 0) {
            const uintptr_t teb_address = query_teb_address(thread_id);
            if (teb_address == 0) {
                printf("Could not resolve the TEB address for thread %lu\n", thread_id);
                return;
            }
            print_teb_at(thread_id, teb_address);
            return;
        }

        const auto thread_ids = debuggee_thread_ids();
        if (thread_ids.empty()) {
            printf("No threads found in process %lu\n", globals::process_id);
            return;
        }

        for (const auto id : thread_ids) {
            const uintptr_t teb_address = query_teb_address(id);
            if (teb_address == 0) {
                printf("Could not resolve the TEB address for thread %lu\n\n", id);
                continue;
            }
            print_teb_at(id, teb_address);
        }
    }

    void print_handle_information(const HANDLE handle_value) {
        if (handle_value == 0) {
            print_all_handles();
        } else {
            print_handle_info(handle_value);
        }
    }

    void populate_object_type_indexes() {
        ULONG size {0x4000};

        std::vector<std::byte> object_types_buffer(size);
        NTSTATUS status = NtQueryObject(nullptr, static_cast<::OBJECT_INFORMATION_CLASS>(ObjectTypesInformation), object_types_buffer.data(), sizeof(OBJECT_TYPES_INFORMATION), &size);

        while (status == STATUS_INFO_LENGTH_MISMATCH) {
            object_types_buffer.resize(size);
            status = NtQueryObject(nullptr, static_cast<::OBJECT_INFORMATION_CLASS>(ObjectTypesInformation), object_types_buffer.data(), size, &size);
        }

        if (NT_SUCCESS(status)) {
            std::span<OBJECT_TYPES_INFORMATION> object_types_info {reinterpret_cast<OBJECT_TYPES_INFORMATION*>(object_types_buffer.data()), 1};
            const auto type_count = object_types_info[0].NumberOfTypes;

            // OBJECT_TYPES_INFORMATION from NtQueryObject returns OBJECT_TYPES_INFORMATION + x * OBJECT_TYPE_INFORMATION this is poorly documented but has been like this for a couple versions
            // Also just to be fun, these structs are aligned on 8 byte boundaries and the string buffer is at the end which can mess with alignment 
            OBJECT_TYPE_INFORMATION *type = reinterpret_cast<OBJECT_TYPE_INFORMATION *>(
                object_types_buffer.data() 
                + align_up(sizeof(OBJECT_TYPES_INFORMATION), sizeof(ULONG_PTR))
            );

            for (uint32_t i = 0; i < type_count; i++) {
                std::wstring type_name(type->TypeName.Buffer, type->TypeName.Length / sizeof(WCHAR));
                object_type_index_to_name[type->TypeIndex] = wstring_to_string(type_name);

                // now reinterpret again and realign
                type = reinterpret_cast<OBJECT_TYPE_INFORMATION *>(
                    reinterpret_cast<unsigned char *>(type) 
                    + sizeof(OBJECT_TYPE_INFORMATION) 
                    + align_up(type->TypeName.MaximumLength, sizeof(ULONG_PTR))
                );
            }
        }
    }

    void print_tls_information(const int64_t tls_index, const DWORD thread_id) {
        if (tls_index == -1) {
            PROCESS_BASIC_INFORMATION pbi {};
            if (!NT_SUCCESS(NtQueryInformationProcess(globals::process_handle, ProcessBasicInformation,
                                                    &pbi, sizeof(pbi), nullptr))) {
                printf("Could not query the process's PEB address (error %lu)\n", GetLastError());
                return;
            }

            FULL_PEB peb;
            read_remote(pbi.PebBaseAddress, peb);

            printf("TLS slots on thread: %lx\n", thread_id);
            const auto allocated_tls_slots = get_indices_of_true_bits(peb.TlsBitmapBits[0], 0);
            const auto allocated_tls_slots_2 = get_indices_of_true_bits(peb.TlsBitmapBits[1], 32);
            for (const auto index : allocated_tls_slots) {
                printf("0x%04llx : %0lld\n", index, read_thread_tls(thread_id, index));
            }
            for (const auto index : allocated_tls_slots_2) {
                printf("0x%04llx : %0lld\n", index, read_thread_tls(thread_id, index));
            }
        } else {
            printf("0x%04llx : %0lld\n", tls_index, read_thread_tls(thread_id, tls_index));
        }
    }
}
