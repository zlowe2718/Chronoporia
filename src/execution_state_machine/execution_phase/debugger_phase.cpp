#include "debugger_phase.h"
#include "debugger_cli.h"
#include "error_transition.h"
#include "globals.h"
#include "trampoline_manager.h"
#include "transition_variant.h"
#include <argparse/argparse.hpp>
#include <cctype>
#include <iostream>
#include <optional>
#include <string>
#include <vector>


namespace {

    std::string UserInput(const std::string& prompt) {
        std::string result;
        printf("%s", prompt.c_str());
        std::getline(std::cin, result);
        return result;
    }

    // Splits input on whitespace into argv-style tokens for argparse, treating
    // quoted substrings (single or double quotes) as a single token.
    std::vector<std::string> SplitInput(const std::string& input) {
        // argparse ignores the first arg so we add an empty string to initialize
        std::vector<std::string> args {""};
        std::string token;
        bool in_token = false;
        char quote_char = '\0';

        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];

            if (quote_char != '\0') {
                if (c == quote_char) {
                    quote_char = '\0';
                } else {
                    token += c;
                }
                continue;
            }

            if (c == '"' || c == '\'') {
                quote_char = c;
                in_token = true;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(c))) {
                if (in_token) {
                    args.push_back(token);
                    token.clear();
                    in_token = false;
                }
                continue;
            }

            token += c;
            in_token = true;
        }

        if (in_token)
            args.push_back(token);

        return args;
    }
}

namespace chronoporia {

    void DebuggerPhase::Enter() {}

    Transitions DebuggerPhase::Run() {
        while (globals::running) {
            if (process_suspended_) {
                globals::logger->flush_log();
                std::string input = UserInput("chronoporia> ");

                auto args = SplitInput(input);
                if (args.empty()) {
                    printf("Bad string supplied\n");
                    continue;
                }

                // Need to recreate the cli every time because argparse doesn't clear cached states
                DebuggerCLI debugger_cli {};
                auto transtion = debugger_cli.handle_cli(args);

                if (transtion.has_value()) return std::move(transtion.value());
                continue;
            }
        }
        return TransitionToError { false, 0};
    }

    void DebuggerPhase::Exit() {}
}
