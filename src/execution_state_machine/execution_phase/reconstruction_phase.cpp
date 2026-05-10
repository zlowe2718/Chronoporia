#include "reconstruction_phase.h"
#include "trampoline.h"
#include "breakpoint_manager.h"
#include "nt_wrappers.h"
#include "transition.h"

/** TODO: Goals to accomplish here 
* Have plugin load breakpoints at all lines of the program 
* Run plugin initialization function
* Place breakpoint at record code stop and run until code end breakpoint is hit
* Track breakpoints at code addresses as UserBreakpoints
* On UserBreakpoint hit, run the plugin ReachedBreakpoint call to determine if valid
* If breakpoint valid then create a line snapshot event
* Line snapshot will snapshot all thread contexts and only dirty memory
* If we hit a non-det event then either inject values to replay or return the saved value as needed
* Make a per event thread log
* Compare events that a thread hits to per-thread event log to determine value inject or return replay (or both if side effects happen behind the scenes)
* Recreate event_log events with the new Line Snapshot events -> Start from coarse snapshot event and as new events come in compare to old event log and add them in the new order
* On code end breakpoint hit transition to debugger phase. Debugger phase will allow the user to jump between lines and inspect memory like a normal debugger.
* When jumping between lines may want to manually call prints that would normally happen
* Maybe a separate mode where user selects if they want to jump forward in memory or have the program execute forward in memory.  Going backwards still needs to jump back in memory
*/

namespace chronoporia {

    void ReconstructionPhase::Enter() {
        CreateTrampolineRegion();
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtCreateThreadEx));
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtTerminateThread));
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(LdrLoadDll));
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(LdrUnloadDll));
        FinalizeTrampolineRegion();

        InitializePlugins();
    }


    Transition ReconstructionPhase::Run() {

    }

    void ReconstructionPhase::Exit() {
        RemoveAllPermanentBreakpoints();
        DestroyTrampolineRegion();
    }

}