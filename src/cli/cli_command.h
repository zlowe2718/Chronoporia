#pragma once

#include "transition_variant.h"
#include <optional>

namespace chronoporia {

    class CLICommand {
    public:
        CLICommand() {};
        // change return to std::expected<std::optional<Transitions>, std::string>? so we can represent error, no value, and value states?
        virtual std::optional<Transitions> handle_command() = 0;
    };


}