#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>

namespace chronoporia {

    struct TransitionToTimeRestore {
        bool process_suspended;
        uint64_t target_sequence;
    };
}