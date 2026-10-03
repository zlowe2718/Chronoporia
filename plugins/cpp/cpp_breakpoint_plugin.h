#pragma once

#include "base_breakpoint_plugin.h"
#include "predecessor_analysis.h"
#include <string>

// TODO: change to use protobuf to pass data back and forth?  This lets anyone write plugins in their own language
namespace breakpoint_plugin {
    extern "C" {
        __declspec(dllexport) PluginMeta get_metadata();
        __declspec(dllexport) bool initialize_plugin(DWORD debugee_process_id, const std::wstring &dll_name, const std::wstring &pdb_path);
        __declspec(dllexport) void uninitialize_plugin();
        __declspec(dllexport) SourceLocation get_source_info_for_address(const uintptr_t original_address);
        __declspec(dllexport) bool create_conditional_expression(const uintptr_t original_address, const std::string &expression);
        __declspec(dllexport) bool reached_breakpoint(const uintptr_t true_address, const DWORD thread_id);
        __declspec(dllexport) void print_process_info(const uintptr_t original_address, const DWORD thread_id, const ProcessInformationType information_type);
        __declspec(dllexport) uintptr_t get_address_from_name(const std::string& name, const std::string& symbol_type);
        __declspec(dllexport) FunctionBounds get_function_bounds(const uintptr_t original_address);
        __declspec(dllexport) bool add_branch_instrumentation(const uint32_t tls_counter_index, const uint32_t tls_target_index);
        __declspec(dllexport) uintptr_t get_true_address(const uintptr_t original_address);
        __declspec(dllexport) uintptr_t get_original_address(const uintptr_t true_address);
        __declspec(dllexport) uintptr_t get_frame_return_address(const DWORD thread_id);
        __declspec(dllexport) uint64_t get_all_frame_return_addresses(const DWORD thread_id, ReturnAddress* return_addresses, const uint64_t return_size);
        // returns the size of the array if the size isn't big enough
        __declspec(dllexport) uint64_t get_address_predecessors(const uintptr_t true_address, const uint64_t current_branch_counter, const ReverseStepMode reverse_step_mode, PotentialPredecessor* predecessors, const uint64_t predecessors_size);
        __declspec(dllexport) uint64_t get_branch_target(const uintptr_t true_address, const uint64_t current_branch_counter);
        
        __declspec(dllexport) void print_stack(const DWORD thread_id);
        __declspec(dllexport) void print_registers(const DWORD thread_id);
        __declspec(dllexport) void print_lines(const uintptr_t original_address, int16_t line_start, int16_t line_end);
        __declspec(dllexport) void print_object_type(const char* type_name, const uint64_t name_size, const char* dll_name, const uint64_t dll_name_size, const uintptr_t data_address);

        __declspec(dllexport) uint64_t get_breakpoints_for_step(const uintptr_t true_address, const uint32_t step_mode, StepBreakpoint *step_breakpoints, const uint64_t bp_size);
    }
    void print_all_scoped_variables(const OriginalAddress original_address, const DWORD thread_id);
}