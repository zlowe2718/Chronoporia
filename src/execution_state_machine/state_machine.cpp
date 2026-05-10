#include "state_machine.h"
#include "base_execution.h"
#include "reconstruction_phase.h"
#include "process_start_phase.h"
#include "record_phase.h"
#include "error_phase.h"
#include "time_restore_phase.h"
#include "playback_phase.h"
#include "globals.h"
#include "transitions/reconstruction_transition.h"
#include <memory>

namespace chronoporia {

    struct TransitionVisitor {

        std::unique_ptr<BaseExecutionPhase> operator()(TransitionToRecording&& t) {
            return std::make_unique<RecordingPhase>(std::move(t));
        }

        std::unique_ptr<BaseExecutionPhase> operator()(TransitionToError&& t) {
            return std::make_unique<ErrorPhase>(std::move(t));
        }

        std::unique_ptr<BaseExecutionPhase> operator()(TransitionToTimeRestore&& t) {
            return std::make_unique<TimeRestorePhase>(std::move(t));
        }

        std::unique_ptr<BaseExecutionPhase> operator()(TransitionToReconstruction&& t) {
            return std::make_unique<ReconstructionPhase>(std::move(t));
        }

        std::unique_ptr<BaseExecutionPhase> operator()(TransitionToPlayback&& t) {
            return std::make_unique<PlaybackPhase>(std::move(t));
        }

    };

    void RunExecution() {
        std::unique_ptr<BaseExecutionPhase> current_phase = std::make_unique<ProcessStartPhase>();
        
        current_phase->Enter();
        auto transition = current_phase->Run();
        current_phase->Exit();

        TransitionVisitor visitor{};

        while (globals::running) {
            current_phase = std::visit(visitor, std::move(transition));
            current_phase->Enter();
            transition = current_phase->Run();
            current_phase->Exit();
        }

    }

}