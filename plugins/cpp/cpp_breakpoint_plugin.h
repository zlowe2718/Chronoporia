#include "../base_breakpoint_plugin.h"

namespace breakpoint_plugin {

    class CppBreakpointPlugin : BaseBreakpointPlugin {
    public:
        // Any initialization that needs to be run such as loading a debug file
        virtual void InitializePlugin() override;

        virtual void InitializeBreakpointsAtAllLines() override;

        virtual void SetBreakpoint(SourceLocation) override;
        virtual void ClearBreakpoint(SourceLocation) override;
        virtual void ClearAllBreakpoints() override;

        // The plugin is responsible for implementing if this breakpoint has been reached 
        // For example if the user has a breakpoint for a certain line in python this would
        // only return true if that co_line was reached since the breakpoint would be placed
        // at every PyEval_Frame or whatever that call is
        virtual bool ReachedBreakpoint(const uintptr_t code_address) override;
    };

}