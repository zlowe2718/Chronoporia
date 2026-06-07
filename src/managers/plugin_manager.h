#pragma once
#include <Windows.h>
#include <string>


namespace chronoporia {
    typedef void (* InitializePlugin)(DWORD pid, const std::wstring &dll_name, const std::wstring &dll_path);
    typedef void (* UninitializePlugin)();


    struct PluginBreakpoint {
        uintptr_t code_address;
        std::string plugin_language;
    };

    // Initialize plugins
    void LoadAndInitializePlugins();

    // Sets breakpoints at all lines for the selected plugin
    void SetAllBreakpointForPlugin(std::string plugin_language);
    void RemoveAllBreakpointsForPlugin(std::string plugin_language);

    // Maybe instead of set and remove I do a testBreakpoint that automatically handles setting and removing?
    void SetPluginBreakpoint(const PluginBreakpoint& plugin_breakpoint);
}