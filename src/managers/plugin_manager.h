#pragma once

#include "base_breakpoint_plugin.h"
#include <Windows.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

//  TODO: For something like python files how would I switch between the python and the c++ debuggger?  Should there be a preference the user specifies or how should the user say
//  They want to see both python stacks and c++ stacks for instance?

namespace chronoporia {
    typedef breakpoint_plugin::PluginMeta (* GetMetadata)();
    typedef bool (* InitializePlugin)(DWORD pid, const std::wstring &dll_name, const std::wstring &dll_path);
    typedef void (* UninitializePlugin)();
    typedef breakpoint_plugin::SourceLocation (* GetSourceInfoForAddress)(const uintptr_t original_address);
    typedef bool (* CreateConditionalExpression)(const uintptr_t original_address, const std::string &expression);
    typedef bool (* ReachedBreakpoint)(const uintptr_t true_address, const DWORD thread_id);
    typedef void (* PrintProcessInfo)(const uintptr_t original_address, const DWORD thread_id, const breakpoint_plugin::ProcessInformationType information_type);
    typedef uintptr_t (* GetAddressFromName)(const std::string& name, const std::string symbol_type);
    typedef breakpoint_plugin::FunctionBounds (* GetFunctionBounds)(const uintptr_t original_address);
    typedef bool (* AddBranchInstrumentation)(const uint32_t branch_counter_tls_index, const uint32_t branch_target_tls_index);
    typedef uintptr_t (* GetTrueAddress)(const uintptr_t original_address);
    typedef uintptr_t (* GetOriginalAddress)(const uintptr_t true_address);
    typedef uintptr_t (* GetFrameReturnAddress)(const DWORD thread_id);
    typedef uint64_t (* GetAddressPredecessors)(const uintptr_t true_address, const uint64_t current_branch_counter, const breakpoint_plugin::ReverseStepMode reverse_step_mode, breakpoint_plugin::PotentialPredecessor* predecessors, uint64_t predecessors_size);
    typedef void (* PrintStack)(const DWORD thread_id);
    typedef void (* PrintRegisters)(const DWORD thread_id);
    // line_start/line_end are offsets relative to the line code_address maps to
    typedef void (* PrintLines)(const uintptr_t original_address, int16_t line_start, int16_t line_end);
    typedef uint64_t (* GetBreakpointsForStep)(const uintptr_t true_address, const uint32_t step_mode, breakpoint_plugin::StepBreakpoint *step_breakpoints, const uint64_t bp_size);
    typedef uint64_t (* GetBranchTarget)(const uintptr_t true_address, const uint64_t current_branch_counter);
    typedef uint64_t (* GetAllFrameReturnAddresses)(const DWORD thread_id, breakpoint_plugin::ReturnAddress* return_addresses, const uint64_t return_size);
    typedef void (* PrintObjectType)(const char* type_name, const uint64_t name_size, const char* dll_name, const uint64_t dll_name_size, const uintptr_t data_address);

    using PluginLanguages = std::vector<std::string>;

    struct PluginApi {
        GetMetadata get_metadata;
        InitializePlugin initialize_plugin;
        UninitializePlugin uninitialize_plugin;
        GetSourceInfoForAddress get_source_info_for_address;
        CreateConditionalExpression create_conditional_expression;
        ReachedBreakpoint reached_breakpoint;
        PrintProcessInfo print_process_info;
        GetAddressFromName get_address_from_name;
        GetFunctionBounds get_function_bounds;
        AddBranchInstrumentation add_branch_instrumentation;
        GetTrueAddress get_true_address;
        GetOriginalAddress get_original_address;
        GetAddressPredecessors get_address_predecessors;
        PrintStack print_stack;
        PrintRegisters print_registers;
        PrintLines print_lines;
        GetFrameReturnAddress get_frame_return_address;
        GetBreakpointsForStep get_breakpoints_for_step;
        GetBranchTarget get_branch_target;
        GetAllFrameReturnAddresses get_all_frame_return_addresses;
        PrintObjectType print_object_type;
    };

    struct PluginBreakpoint {
        uintptr_t code_address;
        std::string plugin_language;
    };

    // Initialize plugins
    void load_and_initialize_all_plugins();
    void unitialize_all_plugins();
    void print_loaded_plugins();
    void add_branch_counting(const PluginLanguages& plugin_languages);

    uintptr_t get_address_from_name_type(const std::string &plugin_language, const std::string& name, const std::string symbol_type);
    // Calls the plugin get_displaced_address to make sure we have the right address for breakpoints and the like
    uintptr_t get_true_address(const std::string &plugin_language, const uintptr_t original_address);
    uintptr_t get_original_address(const std::string &plugin_language, const uintptr_t true_address);
    uintptr_t get_frame_return_address(const std::string &plugin_language, const DWORD thread_id);
    breakpoint_plugin::SourceLocation get_source_info_for_address(const std::string &plugin_language, const uintptr_t originak_address);

    std::vector<breakpoint_plugin::PotentialPredecessor> get_address_predecesors(const std::string &plugin_language, const uintptr_t true_address, const breakpoint_plugin::ReverseStepMode reverse_step_mode, const DWORD thread_id);
    std::vector<breakpoint_plugin::StepBreakpoint> get_breakpoints_for_step(const std::string& plugin_language, const uintptr_t true_address, const breakpoint_plugin::StepMode mode);
    std::vector<breakpoint_plugin::ReturnAddress> get_all_frame_return_addresses(const std::string &plugin_language, const DWORD thread_id);
    bool create_conditional_breakpoint(const std::string& plugin_language, const uintptr_t original_address, const std::string &expression);
    bool reached_breakpoint(const std::string& plugin_language, const uintptr_t true_address, const DWORD thread_id);

    // This should only be called as a fallback in case get_address_predecessors is empty
    uint64_t get_branch_target(const std::string& plugin_language, const uintptr_t true_address, const DWORD thread_id);

    void print_all_scoped_variables(const uintptr_t original_address, const DWORD thread_id);
    void print_stack(const std::string& plugin_language, const DWORD thread_id);
    void print_registers(const std::string& plugin_language, const DWORD thread_id);
    void print_lines(const std::string& plugin_language, const uintptr_t original_address, const std::span<int16_t> lines);
    void print_object_type(const std::string plugin_language, const std::string& object_type, const std::string& pdb_file, const uintptr_t data_address);

    bool any_plugins_loaded();
    bool plugin_loaded(const std::string &plugin_language);
}