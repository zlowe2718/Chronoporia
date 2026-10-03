#include "utils.h"
#include "base_breakpoint_plugin.h"
#include "branch_instrumentation.h"
#include <TlHelp32.h>
#include <cvconst.h>
#include <dbghelp.h>
#include <fstream>
#include <memoryapi.h>
#include <processthreadsapi.h>
#include <vector>

namespace {
    using namespace breakpoint_plugin;

    // Cap the number of frames walked so a corrupt/cyclic stack can't hang the debugger.
    constexpr int kMaxStackFrames = 64;

    // This is the original address since I fix the Rip when calling StackWalk64
    BOOL CALLBACK read_process_routine(HANDLE process, DWORD64 original_address, PVOID buffer, DWORD size, LPDWORD bytes_read) {
        const auto [true_address, _] = get_true_instruction_address(OriginalAddress {original_address});
        const auto original_bytes = get_original_bytes(true_address);
        if (!original_bytes) {
            ReadProcessMemory(process, reinterpret_cast<void *>(true_address.get()), buffer, size, reinterpret_cast<SIZE_T *>(bytes_read));
            return TRUE;
        }

        memcpy(buffer, original_bytes->data(), size);
        *bytes_read = size;
        return TRUE;
    }

    void fix_thread_ctx(CONTEXT& thread_ctx) {
        const TrueAddress true_address {thread_ctx.Rip};
        const OriginalAddress original_address = get_original_instruction_address(true_address);
        if (original_address.get() != true_address.get()) {
            thread_ctx.Rsp += address_rsp_fix_bytes(true_address);
        }
        thread_ctx.Rip = original_address.get();
    }

    void print_rflags(const CONTEXT& thread_ctx) {
        printf("    RFLAGS Register:\n");
        printf("        CF: %lu\n", (thread_ctx.EFlags >> 0) & 1);
        printf("        PF: %lu\n", (thread_ctx.EFlags >> 2) & 1);
        printf("        AF: %lu\n", (thread_ctx.EFlags >> 4) & 1);
        printf("        ZF: %lu\n", (thread_ctx.EFlags >> 6) & 1);
        printf("        SF: %lu\n", (thread_ctx.EFlags >> 7) & 1);
        printf("        TF: %lu\n", (thread_ctx.EFlags >> 8) & 1);
        printf("        IF: %lu\n", (thread_ctx.EFlags >> 9) & 1);
        printf("        DF: %lu\n", (thread_ctx.EFlags >> 10) & 1);
        printf("        OF: %lu\n", (thread_ctx.EFlags >> 11) & 1);
        printf("        IOPL: %lu\n", (thread_ctx.EFlags >> 12) & 3); // This is bits 12 and 13 for ring level; 0 is kernel, 3 is user mode
        printf("        NT: %lu\n", (thread_ctx.EFlags >> 14) & 1);
        printf("        RF: %lu\n", (thread_ctx.EFlags >> 16) & 1);
        printf("        VM: %lu\n", (thread_ctx.EFlags >> 17) & 1);
        printf("        AC: %lu\n", (thread_ctx.EFlags >> 18) & 1);
        printf("        VIF: %lu\n", (thread_ctx.EFlags >> 19) & 1);
        printf("        VIP: %lu\n", (thread_ctx.EFlags >> 20) & 1);
        printf("        ID: %lu\n", (thread_ctx.EFlags >> 21) & 1);
    }

    // Opens a handle to every thread of process_id, or just to thread_id when it is non-zero.
    // Callers own the returned handles.
    std::vector<breakpoint_plugin::ThreadInfo> collect_threads(const DWORD process_id, const DWORD thread_id) {
        std::vector<breakpoint_plugin::ThreadInfo> thread_infos;
        const bool all_threads = thread_id == 0;

        // Take a snapshot of all threads in the system
        HANDLE thread_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (thread_snapshot == INVALID_HANDLE_VALUE) return thread_infos;

        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);

        // Retrieve information about the first thread
        if (Thread32First(thread_snapshot, &te32)) {
            do {
                // Filter threads belonging to our target process
                if (te32.th32OwnerProcessID != process_id) continue;
                if (!all_threads && te32.th32ThreadID != thread_id) continue;

                HANDLE thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
                if (!thread_handle) continue;
                thread_infos.push_back(breakpoint_plugin::ThreadInfo {thread_handle, te32.th32ThreadID});
            } while (Thread32Next(thread_snapshot, &te32)); // Iterate through the rest
        }

        CloseHandle(thread_snapshot);
        return thread_infos;
    }

    // Prints one frame as: index, PC, symbol+displacement, owning module and source line
    // when the symbol handler can supply them.
    void print_frame(HANDLE process_handle, const int index, const DWORD64 program_counter) {
        // Every frame past the innermost holds a *return* address, which can point at the
        // instruction after the call -- and that may already belong to the next function or
        // line.  Look symbols up one byte back so the call site is reported instead.
        const DWORD64 lookup_address = index == 0 ? program_counter : program_counter - 1;

        printf("    #%-2d 0x%016llx", index, static_cast<unsigned long long>(program_counter));

        // SYMBOL_INFO is variable length: the name is written into the trailing buffer.
        alignas(SYMBOL_INFO) char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] {};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbol_storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 symbol_displacement = 0;
        if (SymFromAddr(process_handle, lookup_address, &symbol_displacement, symbol)) {
            printf(" %s+0x%llx", symbol->Name, static_cast<unsigned long long>(symbol_displacement));
        } else {
            printf(" <unknown>");
        }

        IMAGEHLP_MODULE64 module_info {};
        module_info.SizeOfStruct = sizeof(module_info);
        if (SymGetModuleInfo64(process_handle, lookup_address, &module_info)) {
            printf(" [%s]", module_info.ModuleName);
        }

        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process_handle, lookup_address, &line_displacement, &line)) {
            printf(" (%s:%lu)", line.FileName, line.LineNumber);
        }

        printf("\n");
    }
}

namespace breakpoint_plugin {

    uint64_t get_register_from_id(const CONTEXT &context, DWORD cv_register_id) {
        switch (cv_register_id) {
            case CV_AMD64_RAX: return context.Rax;
            case CV_AMD64_RBX: return context.Rbx;
            case CV_AMD64_RCX: return context.Rcx;
            case CV_AMD64_RDX: return context.Rdx;
            case CV_AMD64_RSI: return context.Rsi;
            case CV_AMD64_RDI: return context.Rdi;
            case CV_AMD64_RBP: return context.Rbp;
            case CV_AMD64_RSP: return context.Rsp;
            case CV_AMD64_R8:  return context.R8;
            case CV_AMD64_R9:  return context.R9;
            case CV_AMD64_R10: return context.R10;
            case CV_AMD64_R11: return context.R11;
            case CV_AMD64_R12: return context.R12;
            case CV_AMD64_R13: return context.R13;
            case CV_AMD64_R14: return context.R14;
            case CV_AMD64_R15: return context.R15;
            default: return 0;
        }
    }

    uintptr_t get_image_base(DWORD process_id, const std::wstring &dll_name) {
        return get_module_range(process_id, dll_name).base;
    }

    ModuleRange get_module_range(DWORD process_id, const std::wstring &dll_name) {
        ModuleRange range{};

        HANDLE snap_handle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);

        if (snap_handle != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 module_entry;
            module_entry.dwSize = sizeof(module_entry);

            if (Module32First(snap_handle, &module_entry)) {
                do {
                    if (std::wstring_view(module_entry.szModule).contains(dll_name.c_str())) {
                        range.base = reinterpret_cast<uintptr_t>(module_entry.modBaseAddr);
                        range.size = module_entry.modBaseSize;
                        break;
                    }
                } while (Module32Next(snap_handle, &module_entry));
            }
        }

        CloseHandle(snap_handle);
        return range;
    }

    std::optional<OriginalAddress> find_rip_in_module(HANDLE process_handle, DWORD thread_id, const ModuleRange &range) {
        if (range.size == 0) return std::nullopt;

        HANDLE thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (!thread_handle) return std::nullopt;

        CONTEXT thread_ctx{};
        thread_ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(thread_handle, &thread_ctx)) {
            CloseHandle(thread_handle);
            return std::nullopt;
        }

        fix_thread_ctx(thread_ctx);

        STACKFRAME64 frame{};
        frame.AddrPC.Offset = thread_ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = thread_ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = thread_ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;

        std::optional<OriginalAddress> found;
        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_handle, thread_handle,
                              &frame, &thread_ctx, read_process_routine, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) break;

            if (frame.AddrPC.Offset >= range.base && frame.AddrPC.Offset < range.base + range.size) {
                found = static_cast<OriginalAddress>(frame.AddrPC.Offset);
                break;
            }
        }

        CloseHandle(thread_handle);
        return found;
    }

    OriginalAddress get_return_address(const HANDLE process_handle, const DWORD thread_id) {
        OriginalAddress null_address {0};
        HANDLE thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (!thread_handle) return null_address;

        CONTEXT thread_ctx {};
        thread_ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(thread_handle, &thread_ctx)) {
            printf("Thread: [%lx] could not read thread context (error %lu)\n", thread_id, GetLastError());
            CloseHandle(thread_handle);
            return null_address;
        }

        OriginalAddress return_address {0};

        fix_thread_ctx(thread_ctx);

        STACKFRAME64 frame {};
        frame.AddrPC.Offset = thread_ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = thread_ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = thread_ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;

        // 0 is the first frame, 1 is the second, get the return address from frame 1
        for (int i = 0; i < 2; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_handle, thread_handle,
                                &frame, &thread_ctx, read_process_routine, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) break;
            if (i == 1) return_address = OriginalAddress {frame.AddrPC.Offset};

        }

        CloseHandle(thread_handle);
        return return_address;
    }

    std::vector<OriginalAddress> get_all_return_addresses(const HANDLE process_handle, const DWORD thread_id) {
        std::vector<OriginalAddress> return_addresses;
        HANDLE thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (!thread_handle) return return_addresses;

        CONTEXT thread_ctx {};
        thread_ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(thread_handle, &thread_ctx)) {
            printf("Thread: [%lx] could not read thread context (error %lu)\n", thread_id, GetLastError());
            CloseHandle(thread_handle);
            return return_addresses;
        }

        fix_thread_ctx(thread_ctx);

        STACKFRAME64 frame {};
        frame.AddrPC.Offset = thread_ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = thread_ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = thread_ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;

        for (int i = 0; i < kMaxStackFrames; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_handle, thread_handle,
                                &frame, &thread_ctx, read_process_routine, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) break;
            if (i >= 1) return_addresses.push_back(OriginalAddress {frame.AddrPC.Offset});
        }

        CloseHandle(thread_handle);
        return return_addresses;        
    }

    // TODO: rename so we're not overloading the other print stack
    void print_stack(HANDLE process_handle, const DWORD process_id, const DWORD thread_id) {
        if (!process_handle) {
            printf("No handle to the debuggee process; cannot walk the stack\n");
            return;
        }

        std::vector<ThreadInfo> thread_infos = collect_threads(process_id, thread_id);
        if (thread_infos.empty()) {
            printf("No threads found for process %lu\n", process_id);
            return;
        }

        for (const auto &thread_info : thread_infos) {
            CONTEXT thread_ctx {};
            thread_ctx.ContextFlags = CONTEXT_FULL;
            if (!GetThreadContext(thread_info.thread_handle, &thread_ctx)) {
                printf("Thread: [%lx] could not read thread context (error %lu)\n", thread_info.thread_id, GetLastError());
                CloseHandle(thread_info.thread_handle);
                continue;
            }

            printf("Thread: [0x%lx]\n", thread_info.thread_id);

            fix_thread_ctx(thread_ctx);

            STACKFRAME64 frame {};
            frame.AddrPC.Offset = thread_ctx.Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrStack.Offset = thread_ctx.Rsp;
            frame.AddrStack.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = thread_ctx.Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;

            for (int i = 0; i < kMaxStackFrames; ++i) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_handle, thread_info.thread_handle,
                                 &frame, &thread_ctx, read_process_routine, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                    break;
                }
                if (frame.AddrPC.Offset == 0) break;

                print_frame(process_handle, i, frame.AddrPC.Offset);

                if (i == kMaxStackFrames - 1) printf("    ... (stopped after %d frames)\n", kMaxStackFrames);
            }

            CloseHandle(thread_info.thread_handle);
        }
    }

    // TODO: support the other registers 
    void print_registers(const DWORD thread_id, const DWORD process_id) {
        std::vector<ThreadInfo> thread_infos = collect_threads(process_id, thread_id);

        for (const auto& thread_info : thread_infos) {
            CONTEXT thread_ctx {};
            thread_ctx.ContextFlags = CONTEXT_FULL;
            GetThreadContext(thread_info.thread_handle, &thread_ctx);

            printf("Thread: [0x%lx]\n", thread_info.thread_id);

            printf("    General Purpose Registers:\n");
            printf("        RAX: %llu\n", thread_ctx.Rax);
            printf("        RBX: %llu\n", thread_ctx.Rbx);
            printf("        RCX: %llu\n", thread_ctx.Rcx);
            printf("        RDX: %llu\n", thread_ctx.Rdx);
            printf("        RIP: %llu\n", thread_ctx.Rip);
            printf("        RSP: %llu\n", thread_ctx.Rsp);
            printf("        RBP: %llu\n", thread_ctx.Rbp);
            printf("        RSI: %llu\n", thread_ctx.Rsi);
            printf("        RDI: %llu\n", thread_ctx.Rdi);
            printf("        R8: %llu\n", thread_ctx.R8);
            printf("        R9: %llu\n", thread_ctx.R9);
            printf("        R10: %llu\n", thread_ctx.R10);
            printf("        R11: %llu\n", thread_ctx.R11);
            printf("        R12: %llu\n", thread_ctx.R12);
            printf("        R13: %llu\n", thread_ctx.R13);
            printf("        R14: %llu\n", thread_ctx.R14);
            printf("        R15: %llu\n", thread_ctx.R15);

            printf("    Segment Registers:\n");
            printf("        CS: %hu\n", thread_ctx.SegCs);
            printf("        DS: %hu\n", thread_ctx.SegDs);
            printf("        SS: %hu\n", thread_ctx.SegSs);
            printf("        ES: %hu\n", thread_ctx.SegEs);
            printf("        FS: %hu\n", thread_ctx.SegFs);
            printf("        GS: %hu\n", thread_ctx.SegGs);

            print_rflags(thread_ctx);

            CloseHandle(thread_info.thread_handle);
        }
    }

    void print_source_lines(const std::wstring &file_path, const uint64_t current_line, const int32_t start_offset, const int32_t end_offset) {
        if (file_path.empty()) {
            printf("No source file is recorded for the current address\n");
            return;
        }

        std::ifstream source_file(file_path);
        if (!source_file) {
            printf("Could not open source file %ls\n", file_path.c_str());
            return;
        }

        // Line numbers are one based, so an offset that runs off the top of the file clamps to
        // the first line instead of wrapping around through the unsigned line counter.
        const int64_t requested_first = static_cast<int64_t>(current_line) + start_offset;
        const int64_t requested_last = static_cast<int64_t>(current_line) + end_offset;
        if (requested_last < 1) return;
        const uint64_t first_line = requested_first < 1 ? 1 : static_cast<uint64_t>(requested_first);
        const uint64_t last_line = static_cast<uint64_t>(requested_last);
        if (last_line < first_line) return;

        printf("%ls\n", file_path.c_str());

        std::string line;
        uint64_t line_number = 0;
        while (line_number < last_line && std::getline(source_file, line)) {
            ++line_number;
            if (line_number < first_line) continue;
            printf("%s %6llu  %s\n", line_number == current_line ? "->" : "  ",
                   static_cast<unsigned long long>(line_number), line.c_str());
        }

        if (line_number < first_line) {
            printf("Requested line %llu is past the end of the file (%llu lines)\n",
                   static_cast<unsigned long long>(first_line), static_cast<unsigned long long>(line_number));
        }
    }

    std::wstring str_to_wstr(const std::string &str)  {
        if (str.empty()) return std::wstring();

        size_t chars_converted = 0;
        size_t required_size = 0;
        mbstowcs_s(&required_size, nullptr, 0, str.c_str(), _TRUNCATE);

        // 2. Allocate space and perform conversion
        std::vector<wchar_t> buffer(required_size);
        mbstowcs_s(&chars_converted, buffer.data(), required_size, str.c_str(), _TRUNCATE);

        return std::wstring(buffer.data());        
    }
}