#pragma once
#include "record_transition.h"
#include "error_transition.h"
#include "time_restore_transition.h"
#include "playback_transition.h"
#include "reconstruction_transition.h"
#include <variant>

namespace chronoporia {

    using Transition = std::variant<TransitionToRecording, TransitionToError, TransitionToTimeRestore, TransitionToPlayback, TransitionToReconstruction>;

}