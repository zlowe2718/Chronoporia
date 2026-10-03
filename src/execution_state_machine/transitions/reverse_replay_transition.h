#pragma once
#include "transition.h"
#include "base_breakpoint_plugin.h"
#include <vector>
#include <Windows.h>

namespace chronoporia {

    struct TransitionToReverseReplay : public Transition {
        std::vector<breakpoint_plugin::PotentialPredecessor> predecessors;
        std::vector<breakpoint_plugin::ReturnAddress> frame_return_addresses;
        DWORD thread_id;
        uint64_t branch_target;
        uintptr_t current_address;
        uint64_t current_branch_counter;
    };

}