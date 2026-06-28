#include <Windows.h>
#include <cstdint>
#include <string>

namespace breakpoint_plugin {
    struct SourceLocation {
        std::wstring file_name;
        uint64_t line_number;
        uint64_t length;
        // This is the physical address in memory where the breakpoint is physically placed 
        uintptr_t code_address;
    };

    // BREAKPOINT API
    // // Any initialization that needs to be run such as loading a debug file
    // void InitializePlugin()
    // void UninitializePlugin()

    // void InitializeBreakpointsAtAllLines()

    // void SetBreakpoint(SourceLocation)
    // void ClearBreakpoint(SourceLocation)
    // void ClearAllBreakpoints()

    // // The plugin is responsible for implementing if this breakpoint has been reached 
    // // For example if the user has a breakpoint for a certain line in python this would
    // // only return true if that co_line was reached since the breakpoint would be placed
    // // at every PyEval_Frame or whatever that call is
    // bool ReachedBreakpoint(const uintptr_t code_address)
}