#pragma once
#include "base_execution.h"

namespace chronoporia {

    // This mode is for tracking when the child process actually starts executing
    class ProcessStartPhase: public BaseExecutionPhase {
    public:
        void Enter() override;
        Transition Run() override;
        void Exit() override;

    private:
        bool process_loaded = false;
    };

}