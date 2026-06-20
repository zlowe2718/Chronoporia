#pragma once
#include "base_execution.h"
#include "time_restore_transition.h"
#include <cstdint>
#include <future>


namespace chronoporia {

    // This phase is for restoring memory, threads, and dlls to a coarse snapshot or line snapshot
    //  we need this phase in case of dll unloading because we have to inject code into the process and tell
    //  the process to unload certain dlls on its own thread.  Then we listen for the breakpoint that says all done
    class TimeRestorePhase: public BaseExecutionPhase {
    public:
        TimeRestorePhase(TransitionToTimeRestore&& t) 
            : code_size_ {}
            , code_address_ {}
            , process_suspended_ {t.process_suspended}
            , target_run_id_ {t.target_run_id}
            , target_run_sequence_ {t.target_run_sequence}
        {};

        void Enter() override;
        Transition Run() override;
        void Exit() override;
    private:
        bool RunDllRestore();
        bool RunThreadRestore();
        
        DWORD HandleDebugException(const DEBUG_EVENT* debug_event);

        uint64_t code_size_;
        uintptr_t code_address_;
        bool process_suspended_;
        uint32_t target_run_id_;
        uint32_t target_run_sequence_;
        std::future<void> unload_dlls_and_threads_;
    };

}