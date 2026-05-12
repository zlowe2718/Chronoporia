#pragma once
#include "base_execution.h"

namespace chronoporia {

    // This mode is after the reconstruction phase where we now have all the data necessary to jump around in memory from line to line
    //  In this mode we show the user the sequential events across all threads and allow them to select an event to jump to it 
    //  On Line select we jump to that memory in time without needing to replay anything
    class PlaybackPhase: public BaseExecutionPhase {
    public:
        PlaybackPhase([[maybe_unused]] TransitionToPlayback&& t) {};

        void Enter() override {};
        Transition Run() override { return TransitionToError {}; };
        void Exit() override {};
    };

}