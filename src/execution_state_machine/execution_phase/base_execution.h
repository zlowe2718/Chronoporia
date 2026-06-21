#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "transition_variant.h"

namespace chronoporia {
    
    class BaseExecutionPhase {
    public:
        virtual ~BaseExecutionPhase() = default;
        virtual void Enter() = 0;
        virtual Transitions Run() = 0;
        virtual void Exit() {};
    };
}