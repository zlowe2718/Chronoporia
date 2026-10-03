#pragma once
#include "base_execution.h"
#include "reconstruction_transition.h"

namespace chronoporia {

    // This mode is after the recording mode where we try to reconstruct the most recent data for later "playback"
    //  In this mode we initially revert to the most recent coarse snapshot's memory and then start replaying
    //  During the reconstruction mode, we take a memory snapshot at every line (user specifiable) and replay the results
    //  of the non-deterministic events we captured previously.  The only new events that are created should be line snapshots
    class ReconstructionPhase: public BaseExecutionPhase {
    public:
        ReconstructionPhase(TransitionToReconstruction&& t) 
            : process_suspended {t.process_suspended}
        {}


        void Enter() override;
        Transitions Run() override;
        void Exit() override;
    private:
        DWORD HandleDebugException(const DEBUG_EVENT* debug_event);
        DWORD HandleBreakpoint(const DEBUG_EVENT *debug_event);

        bool process_suspended;
    };

}