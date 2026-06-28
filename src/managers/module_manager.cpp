#include "module_manager.h"
#include "globals.h"
#include "execution_tree.h"
#include "quill/LogMacros.h"
#include "quill/std/WideString.h"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace {
    // DLL names need to be unique per windows
    std::map<HMODULE, std::wstring> address_to_name;
    std::unordered_map<
        HMODULE, 
        chronoporia::ExecutionTree<chronoporia::DllInfo>
    > process_module_history;
}

namespace chronoporia {

    void TrackDLL(HMODULE dll_handle, std::wstring dll_name, uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        DllInfo new_info {dll_handle, dll_name, true};

        if (process_module_history.contains(dll_handle)) {
            process_module_history.at(dll_handle).AddChild(std::move(new_info), run_id, run_seq, global_seq);
        } else {
            process_module_history.try_emplace(dll_handle, std::move(new_info), run_id, run_seq, global_seq);
        }
        address_to_name[dll_handle] = dll_name;
    }

    // TODO: Loop through all Dlls in process
    void TrackAllCurrentDLLs(uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {

    }

    void UntrackDLL(HMODULE dll_handle, uint64_t global_seq, const uint32_t run_id, const uint32_t run_seq) {
        std::wstring dll_name = address_to_name[dll_handle];

        DllInfo new_info {dll_handle, dll_name, false};

        if (process_module_history.contains(dll_handle)) {
            process_module_history.at(dll_handle).AddChild(std::move(new_info), run_id, run_seq, global_seq);
        } else {
            LOG_WARNING(globals::logger, "Untracking dll {} before tracking", dll_name);
        }        
    }

    std::vector<DllInfo> RestoreDLLsAtSequence(const uint32_t from_run_id, const uint32_t from_run_seq, const uint32_t to_run_id, const uint32_t to_run_seq) {
        std::vector<DllInfo> snapped_dlls {};
        std::vector<DllInfo> dlls_to_free {};

        std::optional<DllInfo> from_dll_info {};
        std::optional<DllInfo> to_dll_info {};

        for (const auto& [_, execution_tree] : process_module_history) {
            to_dll_info = execution_tree.GetState(to_run_id, to_run_seq);
            if (to_dll_info && to_dll_info->loaded) {
                snapped_dlls.push_back(*to_dll_info);
            }

            from_dll_info = execution_tree.GetState(from_run_id, from_run_seq);
            if (from_dll_info && from_dll_info->loaded) {
                dlls_to_free.push_back(*from_dll_info);
            }
        }

        for (const DllInfo& dll_info : snapped_dlls ) {
            const auto dll_itr = std::find_if(dlls_to_free.begin(), dlls_to_free.end(), 
                [&dll_info](const DllInfo& current_dll_info) {return current_dll_info.dll_handle == dll_info.dll_handle;}
            );
            if (dll_itr != dlls_to_free.end()) {
                dlls_to_free.erase(dll_itr);
            } // else load it?
        }

        return dlls_to_free;
    }

    void CreateModuleHistoryBranch(const uint32_t target_run_id, const uint32_t target_run_seq, const uint32_t new_run_id) {
        for (auto& [_, execution_tree] : process_module_history) {
            execution_tree.RevertToState(target_run_id, target_run_seq, new_run_id);
        }      
    }
}