#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include "transition.h"
#include "transitions_box.h"

namespace chronoporia {

    struct TransitionToTimeRestore : public Transition {
        uint32_t target_run_id;
        uint32_t target_run_sequence;
        uint64_t target_global_sequence;
        TransitionsBox next_transition;
    };
}