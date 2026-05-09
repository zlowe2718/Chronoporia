#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "transition.h"

namespace chronoporia {
    
    class BaseExecutionPhase {
    public:
        virtual ~BaseExecutionPhase() = default;
        virtual void Enter() = 0;
        virtual Transition Run() = 0;
        virtual void Exit() {};
    };
}