#include "plugin_manager.h"
#include "base_breakpoint_plugin.h"
#include "globals.h"
#include "quill/LogMacros.h"
#include "misc.h"
#include "thread_utils.h"
#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <libloaderapi.h>
#include <map>


namespace {
    std::map<std::string, chronoporia::PluginApi> plugin_apis;
    std::vector<chronoporia::PluginApi> all_apis;

    bool valid_api(const chronoporia::PluginApi& api, const std::string &path) {
        if (api.get_metadata == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_metadata from plugin {}. Skipping plugin", path);
            return false;            
        }
        if (api.initialize_plugin == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find initialize_plugin from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.uninitialize_plugin == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find uninitialize_plugin from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.get_source_info_for_address == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_source_info_for_address from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.create_conditional_expression == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find create_conditional_expression from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.reached_breakpoint == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find reached_breakpoint from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.print_process_info == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find print_process_info from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.get_address_from_name == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_address_from_name from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.get_function_bounds == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_function_bounds from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.add_branch_instrumentation == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find add_branch_instrumentation from plugin {}. Skipping plugin", path);
            return false;            
        }

        if (api.get_true_address == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_true_address from plugin {}. Skipping plugin", path);
            return false;                 
        } 

        if (api.get_original_address == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_original_address from plugin {}. Skipping plugin", path);
            return false;                 
        } 

        if (api.get_address_predecessors == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_address_predecessors from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.print_stack == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find print_stack from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.print_registers == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find print_registers from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.print_lines == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find print_lines from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.get_frame_return_address == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_frame_return_address from plugin {}. Skipping plugin", path);
            return false;
        }

        if (api.get_breakpoints_for_step == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_breakpoints_for_step from plugin {}. Skipping plugin", path);
            return false;            
        }

        if (api.get_branch_target == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_branch_target from plugin {}. Skipping plugin", path);
            return false;              
        }

        if (api.get_all_frame_return_addresses == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find get_all_frame_return_addresses from plugin {}. Skipping plugin", path);
            return false;              
        }

        if (api.print_object_type == nullptr) {
            LOG_WARNING(globals::logger, "Failed to find print_object_type from plugin {}. Skipping plugin", path);
            return false;             
        }
        return true;
    }

}

namespace chronoporia {

    void load_and_initialize_all_plugins() {
        std::filesystem::path cwd = ".";
        // std::wstring dll_name {L"python311.dll"};
        // std::wstring pdb_path {L"C:/Users/zlowe/anaconda3/envs/3_11_tt/python311.pdb"};
        std::wstring dll_name {L"simple_cpp.exe"};
        std::wstring pdb_path {L"C:/Users/zlowe/Desktop/_dev/chronoporia/build/bin/test_scripts/simple_cpp.pdb"};        

        for (auto& entry : std::filesystem::directory_iterator(cwd)) {
            if (entry.path().extension() != ".dll") continue;

            std::string plugin_path = wstring_to_string(entry.path());
            HMODULE module = LoadLibrary(entry.path().c_str());
            if (!module) {
                LOG_WARNING(globals::logger, "Failed to load dll {}", plugin_path);
                continue;
            }

            PluginApi api {
                (GetMetadata) GetProcAddress(module, "get_metadata"),
                (InitializePlugin) GetProcAddress(module, "initialize_plugin"),
                (UninitializePlugin) GetProcAddress(module, "uninitialize_plugin"),
                (GetSourceInfoForAddress) GetProcAddress(module, "get_source_info_for_address"),
                (CreateConditionalExpression) GetProcAddress(module, "create_conditional_expression"),
                (ReachedBreakpoint) GetProcAddress(module, "reached_breakpoint"),
                (PrintProcessInfo) GetProcAddress(module, "print_process_info"),
                (GetAddressFromName) GetProcAddress(module, "get_address_from_name"),
                (GetFunctionBounds) GetProcAddress(module, "get_function_bounds"),
                (AddBranchInstrumentation) GetProcAddress(module, "add_branch_instrumentation"),
                (GetTrueAddress) GetProcAddress(module, "get_true_address"),
                (GetOriginalAddress) GetProcAddress(module, "get_original_address"),
                (GetAddressPredecessors) GetProcAddress(module, "get_address_predecessors"),
                (PrintStack) GetProcAddress(module, "print_stack"),
                (PrintRegisters) GetProcAddress(module, "print_registers"),
                (PrintLines) GetProcAddress(module, "print_lines"),
                (GetFrameReturnAddress) GetProcAddress(module, "get_frame_return_address"),
                (GetBreakpointsForStep) GetProcAddress(module, "get_breakpoints_for_step"),
                (GetBranchTarget) GetProcAddress(module, "get_branch_target"),
                (GetAllFrameReturnAddresses) GetProcAddress(module, "get_all_frame_return_addresses"),
                (PrintObjectType) GetProcAddress(module, "print_object_type"),
            };

            if(!valid_api(api, plugin_path)) continue;

            api.initialize_plugin(globals::process_id, dll_name, pdb_path);
            auto plugin_meta = api.get_metadata();
            auto languages = split_string(plugin_meta.languages, ';');
            for (const auto language : languages) {
                plugin_apis[language] = api;
            }
            all_apis.push_back(api);
        }
    }

    void unitialize_all_plugins() {
        for (const auto& plugin_api : all_apis) {
            plugin_api.uninitialize_plugin();
        }        
    }

    void add_branch_counting(const PluginLanguages& plugin_languages) {
        if (plugin_apis.size() > 0) {
            auto branch_counter_tls_index = tls_alloc(TlsKind::BranchCounter);
            auto branch_target_tls_index = tls_alloc(TlsKind::BranchTarget);

            for (const auto& language : plugin_languages) {
                if (plugin_apis.contains(language)) {
                    plugin_apis[language].add_branch_instrumentation(branch_counter_tls_index, branch_target_tls_index);
                } else {
                    LOG_ERROR(globals::logger, "Plugin for language {} not found", language);
                }
            }
        } 
    }

    bool create_conditional_breakpoint(const std::string &plugin_language, const uintptr_t original_address, const std::string &expression) {
        return plugin_apis[plugin_language].create_conditional_expression(original_address, expression);
    }

    bool reached_breakpoint(const std::string &plugin_language, const uintptr_t true_address, const DWORD thread_id) {
        return plugin_apis[plugin_language].reached_breakpoint(true_address, thread_id);
    }

    uintptr_t get_address_from_name_type(const std::string &plugin_language, const std::string& name, const std::string symbol_type) {
        return plugin_apis[plugin_language].get_address_from_name(name, symbol_type);
    }

    uintptr_t get_true_address(const std::string &plugin_language, const uintptr_t original_address) {
        return plugin_apis[plugin_language].get_true_address(original_address);
    }

    uintptr_t get_original_address(const std::string &plugin_language, const uintptr_t true_address) {
        return plugin_apis[plugin_language].get_original_address(true_address);
    }

    uintptr_t get_frame_return_address(const std::string &plugin_language, const DWORD thread_id) {
        return plugin_apis[plugin_language].get_frame_return_address(thread_id);
    }

    std::vector<breakpoint_plugin::ReturnAddress> get_all_frame_return_addresses(const std::string &plugin_language, const DWORD thread_id) {
        std::vector<breakpoint_plugin::ReturnAddress> return_addresses(64);

        const auto new_size = plugin_apis[plugin_language].get_all_frame_return_addresses(thread_id, return_addresses.data(), 64);
        if (new_size != 0) {
            return_addresses.resize(new_size);
            plugin_apis[plugin_language].get_all_frame_return_addresses(thread_id, return_addresses.data(), new_size);
        }

        return_addresses.resize(new_size);
        return return_addresses;
    }

    breakpoint_plugin::SourceLocation get_source_info_for_address(const std::string &plugin_language, const uintptr_t original_address) {
        return plugin_apis[plugin_language].get_source_info_for_address(original_address);
    }

    std::vector<breakpoint_plugin::PotentialPredecessor> get_address_predecesors(const std::string &plugin_language, const uintptr_t true_address, const breakpoint_plugin::ReverseStepMode reverse_step_mode, const DWORD thread_id) {
        const auto tls_counter = read_thread_tls(thread_id, TlsKind::BranchCounter);
        std::vector<breakpoint_plugin::PotentialPredecessor> predecessors;
        predecessors.resize(5);

        const auto new_size = plugin_apis[plugin_language].get_address_predecessors(true_address, tls_counter, reverse_step_mode, predecessors.data(), predecessors.size());
        if (new_size > predecessors.size()) {
            predecessors.resize(new_size);
            plugin_apis[plugin_language].get_address_predecessors(true_address, tls_counter, reverse_step_mode, predecessors.data(), predecessors.size());        
        }

        predecessors.resize(new_size);
        return predecessors;
    }

    uint64_t get_branch_target(const std::string& plugin_language, const uintptr_t true_address, const DWORD thread_id) {
        const auto tls_counter = read_thread_tls(thread_id, TlsKind::BranchCounter);
        return plugin_apis[plugin_language].get_branch_target(true_address, tls_counter);
    }

    void print_loaded_plugins() {
        printf("Current plugins loaded:\n");
        for (const auto& [plugin_language, plugin_api] : plugin_apis) {
            printf("    %s\n", plugin_language.c_str());
        }
    }

    void print_all_scoped_variables(const uintptr_t original_address, const DWORD thread_id) {
        for (const auto& [plugin_language, plugin_api] : plugin_apis) {
            printf("All scoped variables for %s\n", plugin_language.c_str());
            plugin_api.print_process_info(original_address, thread_id, breakpoint_plugin::ProcessInformationType::ScopedVariables);
        }        
    }

    void print_stack(const std::string& plugin_language, const DWORD thread_id) {
        return plugin_apis[plugin_language].print_stack(thread_id);
    }

    void print_registers(const std::string& plugin_language, const DWORD thread_id) {
        return plugin_apis[plugin_language].print_registers(thread_id);
    }

    void print_lines(const std::string& plugin_language, const uintptr_t original_address, const std::span<int16_t> lines) {
        if (lines.empty()) return;

        // A lone count means "this many lines starting from the current one", running forwards
        // for a positive count and backwards for a negative one.  Either way it collapses into
        // an offset range with the current line (offset 0) sitting at one end of it.  Two values
        // are already the range.
        int16_t start = lines[0];
        int16_t end = 0;
        if (lines.size() > 1) {
            end = lines[1];
        } else if (lines[0] > 0) {
            start = 0;
            end = lines[0] - 1;
        } else {
            start = lines[0] + 1;
        }

        return plugin_apis[plugin_language].print_lines(original_address, start, end);
    }

    void print_object_type(const std::string plugin_language, const std::string& object_type, const std::string& pdb_file, const uintptr_t data_address) {
        return plugin_apis[plugin_language].print_object_type(object_type.data(), object_type.size(), pdb_file.data(), pdb_file.size(), data_address);
    }

    bool any_plugins_loaded() {
        return plugin_apis.size() > 0;
    }

    bool plugin_loaded(const std::string &plugin_language) {
        return plugin_apis.contains(plugin_language);
    }

    std::vector<breakpoint_plugin::StepBreakpoint> get_breakpoints_for_step(
        const std::string& plugin_language,
        const uintptr_t true_address,
        const breakpoint_plugin::StepMode mode
    ) {
        std::vector<breakpoint_plugin::StepBreakpoint> breakpoints(8);

        const uint32_t step_mode = static_cast<uint32_t>(mode);
        uint64_t needed = plugin_apis[plugin_language].get_breakpoints_for_step(
            true_address, step_mode, breakpoints.data(), breakpoints.size());

        if (needed > breakpoints.size()) {
            breakpoints.resize(needed);
            needed = plugin_apis[plugin_language].get_breakpoints_for_step(
                true_address, step_mode, breakpoints.data(), breakpoints.size());
        }

        breakpoints.resize(needed);
        return breakpoints;
    }
}