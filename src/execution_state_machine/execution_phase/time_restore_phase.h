#pragma once
#include "base_execution.h"
#include <future>


namespace chronoporia {

    // This phase is for restoring memory, threads, and dlls to a coarse snapshot or line snapshot
    //  we need this phase in case of dll unloading because we have to inject code into the process and tell
    //  the process to unload certain dlls on its own thread.  Then we listen for the breakpoint that says all done
    class TimeRestorePhase: public BaseExecutionPhase {
    public:
        TimeRestorePhase(TransitionToTimeRestore&& t) 
            : code_size {}
            , code_address {}
            , process_suspended {t.process_suspended}
            , target_sequence {t.target_sequence}
        {};

        void Enter() override;
        Transition Run() override;
        void Exit() override;
    private:
        bool RunDllRestore();
        bool RunThreadRestore();
        
        DWORD HandleDebugException(const DEBUG_EVENT* debug_event);

        uint64_t code_size;
        uintptr_t code_address;
        bool process_suspended;
        uint64_t target_sequence;
        std::future<void> unload_dlls_and_threads;
    };

}