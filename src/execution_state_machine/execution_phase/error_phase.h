#pragma once
#include "base_execution.h"
#include "globals.h"

namespace chronoporia {

    // This mode is for error states in chronoporia to gracefully shutdown
    class ErrorPhase: public BaseExecutionPhase {
    public:
        ErrorPhase(TransitionToError&& t) {
            last_error = t.last_error;
        }

        void Enter() override {
            globals::running = false;
        };
        
        Transition Run() override {
            return TransitionToError {};
        };

    private:
        DWORD last_error;
    };

}