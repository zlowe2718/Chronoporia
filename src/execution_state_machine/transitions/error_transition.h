#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace chronoporia {

    struct TransitionToError{
        DWORD last_error;
    };

}