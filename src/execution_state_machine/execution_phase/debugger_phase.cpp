#include "debugger_phase.h"
#include "error_transition.h"
#include "globals.h"
#include "time_restore_transition.h"
#include <cstring>
#include <iostream>
#include <optional>
#include <string>


namespace {

    std::string UserInput(const std::string& prompt) {
        std::string result;
        printf("%s", prompt.c_str());
        std::getline(std::cin, result);
        return result;
    }

    struct Token {
        std::string command;
        std::string value;
    };

    std::optional<Token> Tokenize(std::string_view input) {
        // Trim leading whitespace
        auto start = input.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            return std::nullopt; // empty/whitespace-only input

        input = input.substr(start);

        // Find end of command (first whitespace after it)
        auto cmd_end = input.find_first_of(" \t\r\n");

        std::string command;
        std::string value;

        if (cmd_end == std::string_view::npos) {
            // No whitespace found — entire input is the command, no value
            command = std::string(input);
        } else {
            command = std::string(input.substr(0, cmd_end));

            // Trim leading whitespace from the remainder
            auto val_start = input.find_first_not_of(" \t\r\n", cmd_end);
            if (val_start != std::string_view::npos) {
                // Trim trailing whitespace from value
                auto val_end = input.find_last_not_of(" \t\r\n");
                value = std::string(input.substr(val_start, val_end - val_start + 1));
            }
        }

        return Token{ std::move(command), std::move(value) };
    }
}

// TODO: Need to update the snapshot tracker to support in between snapshots and potential multiple snapshots at similar times.  Maybe add a run counter with a run seq.  Like we play once then
//  revert to the most recent coarse snapshot.  Now we're on run 2 when we replay and any new snapshots start at run seq 1 with 0 being the branch point (or the coarse snapshot).  Need a way to
//  demarcate the branch point.  Maybe shared ptr?

namespace chronoporia {

    void DebuggerPhase::Enter() {}

    Transitions DebuggerPhase::Run() {
        while (globals::running) {
            if (process_suspended_) {
                std::string input = UserInput("chronoporia> ");

                auto token = Tokenize(input);
                if (!token.has_value()) {
                    printf("Bad string supplied\n");
                    continue;
                }

                if (strcmp(token->command.c_str(), "snapshot")) {
                    // TODO: Fix this later with a better way to transition back to the debugger
                    return TransitionToTimeRestore {{true}, 0, 0, TransitionsBox{Transitions{TransitionToDebugger {}}}};
                }
            }
        }
        return TransitionToError { false, 0};
    }

    void DebuggerPhase::Exit() {}
}

