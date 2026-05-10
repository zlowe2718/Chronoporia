#include <string>

namespace breakpoint_plugin {
    struct SourceLocation {
        std::string file_name;
        uint64_t line_number;
        // This is the physical address in memory where the breakpoint is physically placed 
        uintptr_t code_address;
    };

    class BaseBreakpointPlugin {
    public:
        virtual ~BaseBreakpointPlugin() = default;

        // Any initialization that needs to be run such as loading a debug file
        virtual void InitializePlugin() = 0;

        virtual void InitializeBreakpointsAtAllLines() = 0;

        virtual void SetBreakpoint(SourceLocation) = 0;
        virtual void ClearBreakpoint(SourceLocation) = 0;
        virtual void ClearAllBreakpoints() = 0;

        // The plugin is responsible for implementing if this breakpoint has been reached 
        // For example if the user has a breakpoint for a certain line in python this would
        // only return true if that co_line was reached since the breakpoint would be placed
        // at every PyEval_Frame or whatever that call is
        virtual bool ReachedBreakpoint(const uintptr_t code_address) = 0;
    };

}