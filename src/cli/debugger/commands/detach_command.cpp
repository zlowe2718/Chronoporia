#include "detach_command.h"
#include "globals.h"
#include "trampoline_manager.h"
#include <optional>

namespace chronoporia {

    std::optional<Transitions> DetachCommand::handle_command() {
        if (!DebugActiveProcessStop(globals::process_id)) {
            printf("Detach failed with error %ld\n", GetLastError());
            globals::running = false;
            return TransitionToError { false, 0 };
        }
        UnloadTrampoline();
        return std::nullopt;
    }

}