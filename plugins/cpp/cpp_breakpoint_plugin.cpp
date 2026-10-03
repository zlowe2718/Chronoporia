#include "cpp_breakpoint_plugin.h"
#include "base_breakpoint_plugin.h"
#include "branch_instrumentation.h"
#include "compiler.h"
#include "predecessor_analysis.h"
#include "utils.h"
#include "pdb.h"
#include <TlHelp32.h>
#include <atlcomcli.h>
#include <combaseapi.h>
#include <cstddef>
#include <cstdint>
#include <cvconst.h>
#include <dbghelp.h>
#include <dia2.h>
#include <atlbase.h>
#include <processthreadsapi.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <winerror.h>
#include <winnt.h>
#include <winternl.h>

namespace {
    using namespace breakpoint_plugin;

    std::unordered_set<DWORD> loaded_source_file_ids;
    uintptr_t image_base = 0;
    breakpoint_plugin::ModuleRange module_range;
    HANDLE process_handle = nullptr;
    DWORD process_id = 0;
    std::unordered_map<OriginalAddress, breakpoint_plugin::CompiledCondition> conditional_breakpoints;

}

namespace breakpoint_plugin {

    PluginMeta get_metadata() {
        PluginMeta metadata {"c++;cpp"};
        return metadata;
    }

    // TODO: update main to take in a path or folder to search
    // TODO: What should the behaviour be if we can't load pdb information? Fall back to smaller snapshots and replaying manually?
    // TODO: update this to load all pdbs in a current working directory, or search paths (delimited by semicolon)
    //      When loading pdbs grab the guid then enumerate all modules and get their guids.  comparing guids will match the pdb to the exe/dll
    // TODO: Also update to optionally download microsoft pdbs into a server path or hardcoded path
    bool initialize_plugin(DWORD debugee_process_id, const std::wstring &dll_name, const std::wstring &pdb_path) {
        bool success = load_pdb(pdb_path);
        if (!success) {
            printf("Failed to load debug information for the program\n");
            return false;
        }

        module_range = get_module_range(debugee_process_id, dll_name);
        image_base = module_range.base;
        process_id = debugee_process_id;

        process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SUSPEND_RESUME | PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_DUP_HANDLE, FALSE, debugee_process_id);
        if (!process_handle) {
            printf("Failed to open process %lu for reading variable values\n", debugee_process_id);
            return false;
        }

        if (!SymInitialize(process_handle, nullptr, TRUE)) {
            printf("Failed to initialize symbol handler for stack walking\n");
        }

        return true;
    }

    void uninitialize_plugin() {
        for (auto &[address, condition] : conditional_breakpoints) {
            if (condition.module) FreeLibrary(condition.module);
        }
        conditional_breakpoints.clear();

        if (process_handle) {
            SymCleanup(process_handle);
            CloseHandle(process_handle);
            process_handle = nullptr;
        }
        process_id = 0;

        unload_pdb();

    }

    SourceLocation get_source_info_for_address(uintptr_t original_address) {
        return load_breakpoint_from_pdb(OriginalAddress {original_address}, image_base);
    }

    // TODO: think of a way to link against and call debugee functions too
    bool create_conditional_expression(uintptr_t original_address, const std::string &expression) {
        OriginalAddress wrapped_addr {original_address};
        std::vector<VariableInfo> vars = resolve_variables(wrapped_addr, image_base);
        std::string source = build_conditional_source_code(expression, vars);

        wchar_t temp_dir[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_dir);
        std::wstring dll_path = std::wstring(temp_dir) + L"cnd_" + std::to_wstring(original_address) + L".dll";

        if (!compile_condition(source, dll_path)) return false;

        HMODULE module = LoadLibraryW(dll_path.c_str());
        if (!module) {
            printf("Failed to load compiled condition DL\n");
            return false;
        }

        auto evaluate = reinterpret_cast<bool (__cdecl *)(const int64_t *)>(GetProcAddress(module, "evaluate_condition"));
        if (!evaluate) {
            printf("Compiled condition DLL is missing evaluate_condition export\n");
            FreeLibrary(module);
            return false;
        }

        CompiledCondition condition{};
        condition.module = module;
        condition.evaluate = evaluate;
        condition.variables = std::move(vars);

        auto existing = conditional_breakpoints.find(wrapped_addr);
        if (existing != conditional_breakpoints.end() && existing->second.module) {
            FreeLibrary(existing->second.module);
        }
        conditional_breakpoints[wrapped_addr] = std::move(condition);
        return true;
    }

    bool reached_breakpoint(const uintptr_t true_address, const DWORD thread_id) {
        const auto original_address = get_original_instruction_address(TrueAddress {true_address});
        auto itr = conditional_breakpoints.find(original_address);
        if (itr == conditional_breakpoints.end()) return true;

        std::vector<int64_t> args = pack_incoming_args(itr->second.variables, thread_id, process_handle);
        return itr->second.evaluate(args.data());
    }

    void print_process_info(const uintptr_t original_address, const DWORD thread_id, const ProcessInformationType information_type) {
        switch (information_type) {
            case ProcessInformationType::ScopedVariables: {
                print_all_scoped_variables(OriginalAddress {original_address}, thread_id);
                break;
            }
        }
    }

    void print_all_scoped_variables(const OriginalAddress original_address, const DWORD thread_id) {
        OriginalAddress address = original_address;
        if (address < module_range.base || address >= module_range.base + module_range.size) {
            auto found = find_rip_in_module(process_handle, thread_id, module_range);
            if (!found) {
                printf("No frame within the target module was found on this thread's stack\n");
                return;
            }
            address = *found;
        }
        print_all_pdb_scoped_variables(address, thread_id, image_base, process_handle);
    }

    uintptr_t get_address_from_name(const std::string& name, const std::string& symbol_type) {
        if (symbol_type == "function") {
            return get_address_from_func_name(name, image_base).get(); 
        } else if (symbol_type == "label") {
            return get_address_from_c_label(name, image_base).get();
        } else if (symbol_type == "file_line") {
            return get_address_from_file_line(name, image_base).get();
        }
        return 0;
    }

    FunctionBounds get_function_bounds(const uintptr_t original_address) {
        return get_function_bounds_from_address(OriginalAddress {original_address}, image_base);
    }

    bool add_branch_instrumentation(const uint32_t tls_counter_index, const uint32_t tls_target_index) {
        uint32_t tls_counter_offset;
        // Index 0-63 live inline in TEB.TlsSlots, so gs:[TlsSlots + 8*index] is a
        // direct address the counter-increment stub can reach.
        if (tls_counter_index < 64) {
            tls_counter_offset = static_cast<uint32_t>(offsetof(TEB, TlsSlots) + 8 * tls_counter_index);
        } else {
            // TlsExpansionSlots is a POINTER to a heap array, not inline storage, so
            // a single gs:[disp32] can't address it -- it needs a pointer load first.
            // TlsAlloc returns a low index at process start, so this shouldn't happen;
            // bail loudly rather than emit a stub that corrupts the target's memory.
            printf("Cannot instrument: TLS index %u requires expansion-slot indirection\n", tls_counter_index);
            return false;
        }

        uint32_t tls_target_offset;
        if (tls_target_index < 64) {
            tls_target_offset = static_cast<uint32_t>(offsetof(TEB, TlsSlots) + 8 * tls_target_index);
        } else {
            // TlsExpansionSlots is a POINTER to a heap array, not inline storage, so
            // a single gs:[disp32] can't address it -- it needs a pointer load first.
            // TlsAlloc returns a low index at process start, so this shouldn't happen;
            // bail loudly rather than emit a stub that corrupts the target's memory.
            printf("Cannot instrument: TLS index %u requires expansion-slot indirection\n", tls_target_index);
            return false;
        }

        if(!create_instrumentation_region(process_handle, tls_counter_offset, tls_target_offset,image_base)) return false;

        std::vector<FunctionAnalysis> analysis_cache;
        std::set<OriginalAddress> leader_cache;
        for (const FunctionAnalysis& function : analyze_module(process_handle, image_base)) {
            for (const BasicBlock& block : discover_blocks(function)) {
                if (instrument_block(block.start, block.end, image_base) == 0) {
                    printf("Could not instrument block at 0x%llx (%llu bytes)\n",
                           static_cast<unsigned long long>(block.start.get()),
                           static_cast<unsigned long long>(block.end.get() - block.start.get()));
                    continue;
                }
                leader_cache.insert(block.start);
            }
            analysis_cache.push_back(function);
        }
        init_reverse_step_analysis(process_handle, std::move(analysis_cache), std::move(leader_cache));
        commit_instrumentation_region();
        return true;
    }

    void print_stack(const DWORD thread_id) {
        print_stack(process_handle, process_id, thread_id);
    }

    void print_registers(const DWORD thread_id) {
        print_registers(thread_id, process_id);
    }

    void print_lines(const uintptr_t original_address, const int16_t line_start, const int16_t line_end) {
        const SourceLocation location = get_source_info_for_address(original_address);
        // Addresses inside compiler generated code (thunks, padding) have no line entry, and
        // load_breakpoint_from_pdb reports that as a zeroed out location.
        if (location.line_number == 0) {
            printf("No line information for address 0x%llx\n", static_cast<unsigned long long>(original_address));
            return;
        }

        print_source_lines(location.file_name, location.line_number, line_start, line_end);
    }

    void print_object_type(const char* type_name, const uint64_t name_size, const char* dll_name, const uint64_t dll_name_size, const uintptr_t data_address) {
        std::string object_type(type_name, name_size);
        std::string pdb_file(dll_name, dll_name_size);

        print_object_from_type(object_type, pdb_file, data_address);
    }

    uintptr_t get_true_address(const uintptr_t original_address) {
        return get_true_instruction_address(OriginalAddress {original_address}).first.get();
    }

    uintptr_t get_original_address(const uintptr_t true_address) {
        return get_original_instruction_address(TrueAddress {true_address}).get();
    }

    uintptr_t get_frame_return_address(const DWORD thread_id) {
        return get_return_address(process_handle, thread_id).get();
    }

    uint64_t get_all_frame_return_addresses(const DWORD thread_id, ReturnAddress* return_addresses, const uint64_t return_size) {
        const auto& all_return_addresses = get_all_return_addresses(process_handle, thread_id);

        if (all_return_addresses.size() > return_size) return all_return_addresses.size();

        for (const auto& return_address : all_return_addresses) {
            const bool has_source = load_breakpoint_from_pdb(return_address, image_base).line_number != 0;
            *return_addresses = ReturnAddress {return_address.get(), has_source};
            return_addresses++;
        }
        return all_return_addresses.size();
    }

    uint64_t get_address_predecessors(const uintptr_t true_address, const uint64_t current_branch_counter, const ReverseStepMode reverse_step_mode, PotentialPredecessor* predecessors, const uint64_t predecessors_size) { 
        const auto& potential_predecessors = get_predecessors_of_address(TrueAddress {true_address}, current_branch_counter, reverse_step_mode, image_base);

        uint64_t count = 0;
        for (auto potential_predecessor : potential_predecessors) {
            if (count >= predecessors_size) return potential_predecessors.size();
            *predecessors = potential_predecessor;
            predecessors++;
            count += 1;
        }
        return potential_predecessors.size();
    }

    uint64_t get_branch_target(const uintptr_t true_address, const uint64_t current_branch_counter) {
        const auto original_address = get_original_instruction_address(TrueAddress {true_address});
        return is_instrumented_leader(original_address) ? current_branch_counter - 1 : current_branch_counter;
    }

    uint64_t get_breakpoints_for_step(const uintptr_t true_address, const uint32_t step_mode, StepBreakpoint *step_breakpoints, const uint64_t bp_size) {
        const auto original_address = get_original_instruction_address(TrueAddress {true_address});

        const auto line_ranges = get_line_ranges_from_address(original_address, image_base);
        if (line_ranges.size() == 0) return 0;

        const StepMode mode = static_cast<StepMode>(step_mode);
        std::vector<StepBreakpoint> breakpoints_to_add;        

        const auto add_breakpoint = [&breakpoints_to_add](const OriginalAddress original_address, const StepBreakpointKind kind) {
            for (const auto& breakpoint : breakpoints_to_add) {
                if (breakpoint.original_address == original_address.get()) return;
            }
            const auto [true_address, _] = get_true_instruction_address(original_address);
            const bool has_source = load_breakpoint_from_pdb(original_address, image_base).line_number != 0;
            breakpoints_to_add.push_back({original_address.get(), true_address.get(), kind, has_source});
        };

        const auto function_analysis = function_analysis_containing_address(original_address);
        // TODO: handle this edge case, maybe place breakpoint at every line in the function?
        if (!function_analysis) {
            printf("No Control Flow for function at address %p, can't determine next target", reinterpret_cast<void *>(original_address.get()));
            return 0;
        }

        // 1) Fallthrough exits.  Control walks off the bottom of each range at range.end,
        //    and the line table already gave us that address -- so no instruction in the
        //    middle of a range ever needs decoding.  Skipped when the range ends in
        //    something that cannot fall through.
        for (const auto& line_range : line_ranges) {
            bool falls_through = true;
            for (const BranchSite& branch : function_analysis->branches) {
                if (branch.next_address != line_range.end) continue;
                falls_through = branch.kind != BranchKind::UnconditionalJmp &&
                                branch.kind != BranchKind::IndirectJump &&
                                branch.kind != BranchKind::Return;
                break;
            }
            if (falls_through) add_breakpoint(line_range.end, StepBreakpointKind::Line);
        }

        // 2) Transfers out of the middle of the line.  A branch's *fallthrough* never needs
        //    a stop here: if it left the line then the branch was the last instruction of
        //    its range, so step 1 already planted at that range.end.
        for (const BranchSite& branch : function_analysis->branches) {
            if (!address_in_line_ranges(line_ranges, branch.address)) continue;
            switch (branch.kind) {
                case BranchKind::Conditional:
                case BranchKind::UnconditionalJmp:
                    // A back edge into our own ranges is the loop this line *is*; letting
                    // it run is the whole speed win over single stepping.
                    if (!address_in_line_ranges(line_ranges, branch.target)) add_breakpoint(branch.target, StepBreakpointKind::Line);
                    break;

                // Trap the call site, not the callee entry: it keeps every int3 inside the
                // frame being stepped, and direct and indirect calls need no separate
                // handling.  Costs one single step to learn the target.
                case BranchKind::Call:
                case BranchKind::IndirectCall:
                    if (mode == StepMode::Into) add_breakpoint(branch.address, StepBreakpointKind::Call);
                    break;

                case BranchKind::IndirectJump:
                    add_breakpoint(branch.address, StepBreakpointKind::IndirectJump);
                    break;

                case BranchKind::Return:
                    add_breakpoint(branch.address, StepBreakpointKind::Return);
                    break;
            }
        }

        if (breakpoints_to_add.size() > bp_size) {
            return breakpoints_to_add.size();
        }

        for (uint64_t i = 0; i < breakpoints_to_add.size(); i++) {
            step_breakpoints[i] = breakpoints_to_add[i];
        }

        return breakpoints_to_add.size();
    }
}