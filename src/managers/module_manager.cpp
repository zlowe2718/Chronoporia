#include "module_manager.h"
#include "globals.h"
#include "partially_persistent_arrays.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {
    // DLL names need to be unique per windows
    std::map<HMODULE, std::wstring> address_to_name;
    std::unordered_map<
        HMODULE, 
        chronoporia::PartiallyPersistentArray<chronoporia::DllInfo>
    > process_module_history;
}

namespace chronoporia {

    void TrackDLL(HMODULE dll_handle, std::wstring dll_name, uint64_t global_seq) {
        DllInfo new_info {dll_handle, dll_name, true};

        if (process_module_history.contains(dll_handle)) {
            process_module_history[dll_handle].update(global_seq, new_info);
        } else {
            process_module_history[dll_handle] = PartiallyPersistentArray<DllInfo>(global_seq, new_info);
        }
        address_to_name[dll_handle] = dll_name;
    }

    // Loop through all Dlls in process
    void TrackAllCurrentDLLs(uint64_t global_seq) {

    }

    void UntrackDLL(HMODULE dll_handle, uint64_t global_seq) {
        std::wstring dll_name = address_to_name[dll_handle];

        DllInfo new_info {dll_handle, dll_name, false};

        if (process_module_history.contains(dll_handle)) {
            process_module_history[dll_handle].update(global_seq, new_info);
        } else {
            printf("Untracking dll %ls before tracking", dll_name.c_str());
        }        
    }

    std::vector<DllInfo> RestoreDLLsAtSequence(uint64_t target_seq) {
        std::vector<DllInfo> snapped_dlls {};
        std::vector<DllInfo> dlls_to_free {};

        DllInfo fetched_dll_info {};

        for (const auto& [_, partially_persistent_dll] : process_module_history) {
            if (target_seq >= partially_persistent_dll.created_version) {
                fetched_dll_info = partially_persistent_dll.get(target_seq);
                if (fetched_dll_info.loaded) {
                    snapped_dlls.push_back(fetched_dll_info);
                }
            }

            fetched_dll_info = partially_persistent_dll.get(globals::global_sequence);
            if (fetched_dll_info.loaded) {
                dlls_to_free.push_back(fetched_dll_info);
            }
        }

        for (const DllInfo& dll_info : snapped_dlls ) {
            const auto dll_itr = std::find_if(dlls_to_free.begin(), dlls_to_free.end(), 
                [&dll_info](const DllInfo& current_dll_info) {return current_dll_info.dll_handle == dll_info.dll_handle;}
            );
            if (dll_itr != dlls_to_free.end()) {
                dlls_to_free.erase(dll_itr);
            } // TODO: else load it?
        }

        return dlls_to_free;
    }
}