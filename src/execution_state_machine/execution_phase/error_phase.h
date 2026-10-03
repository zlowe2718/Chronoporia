#pragma once
#include "base_execution.h"
#include "error_transition.h"
#include "globals.h"
#include "transition.h"
#include <errhandlingapi.h>

namespace chronoporia {

    // This mode is for error states in chronoporia to gracefully shutdown
    class ErrorPhase: public BaseExecutionPhase {
    public:
        ErrorPhase(TransitionToError&& t) {
            last_error = t.last_error;
        }

        // Something went wrong if this is hit
        ErrorPhase([[maybe_unused]] Transition&& t) {
            last_error = GetLastError();
        }

        void Enter() override {
            globals::running = false;
        };
        
        Transitions Run() override {
            return TransitionToError {};
        };

    private:
        DWORD last_error;
    };

}