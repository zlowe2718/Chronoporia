#pragma once
#include "transition.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace chronoporia {

    struct TransitionToError : public Transition {
        DWORD last_error;
    };

}