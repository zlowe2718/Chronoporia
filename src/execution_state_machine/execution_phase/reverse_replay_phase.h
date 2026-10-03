#pragma once
#include "base_breakpoint_plugin.h"
#include "base_execution.h"
#include "reverse_replay_transition.h"
#include <optional>
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace chronoporia {
    // define the address and its branch counter, and a final too far branch counter
    // to trap on that says "we went too far and didn't find what we were looking for"
    struct ReverseCatchers {
        uintptr_t catch_address;
        uint64_t catch_branch_counter;
        uint64_t too_far_branch_counter;
    };
    // This mode is after the reconstruction phase where we now have all the data necessary to act as a debugger.
    // Here we can jump between snapshots, play from snapshots, normal step forward, step into /out from, run to next breakpoint
    // as well as the same in reverse.  Have an option to save state when line hit to cache for later?
    // We'll leverage the plugin architecture to decide how to handle breakpoints so that this is language agnostic
    class ReverseReplayPhase: public BaseExecutionPhase {
    public:
        ReverseReplayPhase(TransitionToReverseReplay&& t) 
            : process_suspended_ {t.process_suspended}
            , predecessors_ {std::move(t.predecessors)}
            , frame_return_addresses_ {std::move(t.frame_return_addresses)}
            , replay_thread_id_ {t.thread_id}
            , branch_target_ {t.branch_target}
            , stopped_address_ {t.current_address}
            , stopped_branch_counter_ {t.current_branch_counter}
            , reverse_catchers_ {}
            , pred_last_seen_ctr_ {}
        {};

        void Enter() override;
        Transitions Run() override;
        void Exit() override;
    
    private:
        std::optional<breakpoint_plugin::PotentialPredecessor> get_potential_predecessor(const uintptr_t address);

        bool process_suspended_;
        std::vector<breakpoint_plugin::PotentialPredecessor> predecessors_;
        std::vector<breakpoint_plugin::ReturnAddress> frame_return_addresses_;
        DWORD replay_thread_id_;
        uint64_t branch_target_;
        uintptr_t stopped_address_;
        uint64_t stopped_branch_counter_;
        ReverseCatchers reverse_catchers_;
        std::unordered_map<uintptr_t, uint64_t> pred_last_seen_ctr_;
    };

}