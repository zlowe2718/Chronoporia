#include "record_transition.h"
#include "error_transition.h"
#include "time_restore_transition.h"
#include "reconstruction_transition.h"
#include "debugger_transition.h"
#include "transition.h"
#include <variant>


namespace chronoporia  {

    using Transitions = std::variant<
        Transition,
        TransitionToRecording, 
        TransitionToError, 
        TransitionToTimeRestore, 
        TransitionToReconstruction, 
        TransitionToDebugger
    >;

}