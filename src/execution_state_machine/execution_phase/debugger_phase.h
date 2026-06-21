#pragma once
#include "base_execution.h"
#include "debugger_transition.h"

// TODO: Maybe some kind of graphic eventually like
//                  Event 1          Event 2          Event 3          Event 4
//  thread 123      LineSnap ------> LineSnap2           -                -
//  thread 12          -                -     |---       -                -
//  thread 234         -                -         |-->lineSnap3           -
//  thread 1234        -                -                -             LineSnap4

namespace chronoporia {

    enum class Command {BpAtAddress, JumpToSnapshot};

    // This mode is after the reconstruction phase where we now have all the data necessary to act as a debugger.
    // Here we can jump between snapshots, play from snapshots, normal step forward, step into /out from, run to next breakpoint
    // as well as the same in reverse.  Have an option to save state when line hit to cache for later?
    // We'll leverage the plugin architecture to decide how to handle breakpoints so that this is language agnostic
    class DebuggerPhase: public BaseExecutionPhase {
    public:
        DebuggerPhase([[maybe_unused]] TransitionToDebugger&& t) 
            : process_suspended_ {t.process_suspended}
        {};

        void Enter() override;
        Transitions Run() override;
        void Exit() override;
    
    private:
        bool process_suspended_; 
    };

}