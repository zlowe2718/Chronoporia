#include "plugin_manager.h"
#include "globals.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <libloaderapi.h>


namespace {
    std::vector<chronoporia::UninitializePlugin> plugins_uninitializers;
}

namespace chronoporia {

    void LoadAndInitializePlugins() {
        std::filesystem::path cwd = ".";
        std::wstring dll_name {L"python311.dll"};
        std::wstring pdb_path {L"C:/Users/zlowe/anaconda3/envs/3_11_tt/python311.pdb"};

        for (auto& entry : std::filesystem::directory_iterator(cwd)) {
            if (entry.path().extension() != ".dll") continue;

            HMODULE module = LoadLibrary(entry.path().c_str());
            if (!module) {
                printf("Failed to load dll %ls", entry.path().c_str());
                continue;
            }

            auto* initialize_plugin = (InitializePlugin) GetProcAddress(module, "InitializePlugin");
            initialize_plugin(globals::process_id, dll_name, pdb_path);

            auto* uninitializePlugin = (UninitializePlugin) GetProcAddress(module, "UninitializePlugin");
            plugins_uninitializers.push_back(uninitializePlugin);
        }
    }

    void UninitializePlugins() {
        for (const auto& plugin_uninitialize : plugins_uninitializers) {
            plugin_uninitialize();
        }        
    }
}