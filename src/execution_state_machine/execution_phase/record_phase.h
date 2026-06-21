#pragma once
#include "base_execution.h"
#include "record_transition.h"

namespace chronoporia {

    // RecordingMode is for running the child process initially
    //  We take coarse snapshots of the memory every so often and track all sources of non-determinism as events
    class RecordingPhase: public BaseExecutionPhase {
    public:
        RecordingPhase(TransitionToRecording&& t) 
            : child_entry_address {}
            , process_loaded {}
            , process_suspended {t.process_suspended}
        {}

        void Enter() override;
        Transitions Run() override;
        void Exit() override;
    private:
        Transitions DebugLoop();
        DWORD HandleDebugException(const DEBUG_EVENT* debug_event);
        DWORD HandleBreakpoint(const DEBUG_EVENT *debug_event);
        void SetupNonDetCapture();
    
        uintptr_t child_entry_address;
        bool process_loaded;
        bool process_suspended;
    };

}