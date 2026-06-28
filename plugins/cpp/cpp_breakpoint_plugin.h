#include "../base_breakpoint_plugin.h"
#include <string>



namespace breakpoint_plugin {
    extern "C" {
        // Any initialization that needs to be run such as loading a debug file
        __declspec(dllexport) void InitializePlugin(DWORD process_id, const std::wstring &dll_name, const std::wstring &pdb_path);
        __declspec(dllexport) void UninitializePlugin();

        __declspec(dllexport) void InitializeBreakpointsAtAllLines();

        __declspec(dllexport) void SetBreakpoint(SourceLocation);
        __declspec(dllexport) void ClearBreakpoint(SourceLocation);
        __declspec(dllexport) void ClearAllBreakpoints();

        // The plugin is responsible for implementing if this breakpoint has been reached 
        // For example if the user has a breakpoint for a certain line in python this would
        // only return true if that co_line was reached since the breakpoint would be placed
        // at every PyEval_Frame or whatever that call is
        __declspec(dllexport) bool ReachedBreakpoint(const uintptr_t code_address);
    }

    bool LoadPDB(const std::wstring &path);
    void LoadSourceLocations(uintptr_t image_base);
    uintptr_t GetImageBase(DWORD process_id, const std::wstring &dll_name);
}