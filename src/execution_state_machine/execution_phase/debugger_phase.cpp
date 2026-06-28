#include "debugger_phase.h"
#include "error_transition.h"
#include "globals.h"
#include "nt_wrappers.h"
#include "snapshot_log.h"
#include "thread_utils.h"
#include "time_restore_transition.h"
#include "trampoline.h"
#include "trampoline_manager.h"
#include "breakpoint_manager.h"
#include <cstring>
#include <iostream>
#include <map>
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
        std::map<std::string, std::string> values;
    };

    // Parses values of the form "--key1=value1 --key2=value2 --flag" following the command.
    void TokenizeValues(std::string_view rest, std::map<std::string, std::string>& values) {
        auto pos = rest.find_first_not_of(" \t\r\n");
        while (pos != std::string_view::npos) {
            auto token_end = rest.find_first_of(" \t\r\n", pos);
            std::string_view item = (token_end == std::string_view::npos)
                ? rest.substr(pos)
                : rest.substr(pos, token_end - pos);

            if (item.starts_with("--"))
                item = item.substr(2);

            auto eq = item.find('=');
            if (eq == std::string_view::npos) {
                values.emplace(std::string(item), std::string());
            } else {
                values.emplace(std::string(item.substr(0, eq)), std::string(item.substr(eq + 1)));
            }

            if (token_end == std::string_view::npos)
                break;
            pos = rest.find_first_not_of(" \t\r\n", token_end);
        }
    }

    std::optional<Token> Tokenize(std::string_view input) {
        // Trim leading whitespace
        auto start = input.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            return std::nullopt; // empty/whitespace-only input

        input = input.substr(start);

        // Find end of command (first whitespace after it)
        auto cmd_end = input.find_first_of(" \t\r\n");

        std::string command;
        std::map<std::string, std::string> values;

        if (cmd_end == std::string_view::npos) {
            // No whitespace found — entire input is the command, no values
            command = std::string(input);
        } else {
            command = std::string(input.substr(0, cmd_end));
            TokenizeValues(input.substr(cmd_end), values);
        }

        return Token{ std::move(command), std::move(values) };
    }
}

// TODO: Need to update the snapshot tracker to support in between snapshots and potential multiple snapshots at similar times.  Maybe add a run counter with a run seq.  Like we play once then
//  revert to the most recent coarse snapshot.  Now we're on run 2 when we replay and any new snapshots start at run seq 1 with 0 being the branch point (or the coarse snapshot).  Need a way to
//  demarcate the branch point.  Maybe shared ptr?

// TODO: update to use an actual argparse tool instead of a custom one
namespace chronoporia {

    void DebuggerPhase::Enter() {}

    Transitions DebuggerPhase::Run() {
        while (globals::running) {
            if (process_suspended_) {
                // flush before user input so we have a clean line
                globals::logger->flush_log();
                std::string input = UserInput("chronoporia> ");

                auto token = Tokenize(input);
                if (!token.has_value()) {
                    printf("Bad string supplied\n");
                    continue;
                }

                if (token->command == "snapshot" && token->values.contains("list")) {
                    PrintSnapshotHistory();
                    continue;
                }

                if (strcmp(token->command.c_str(), "snapshot") == 0) {
                    if (token->values["run_id"] == "" || token->values["run_seq"] == "") {
                        printf("Please supply both a run_id and run_seq for the snapshot\n");
                        continue;                        
                    }
                    uint32_t run_id = static_cast<uint32_t>(std::stoul(token->values["run_id"]));
                    uint32_t run_seq = static_cast<uint32_t>(std::stoul(token->values["run_seq"]));
                    // TODO: Fix this later with a better way to transition back to the debugger
                    return TransitionToTimeRestore {{true}, run_id, run_seq, TransitionsBox{Transitions{TransitionToDebugger {true}}}};
                } else if (strcmp(token->command.c_str(), "resume") == 0) {
                    ResumeProgram();
                } else if (strcmp(token->command.c_str(), "detach") == 0) {
                    if (!DebugActiveProcessStop(globals::process_id)) {
                        printf("Detach failed with error %ld\n", GetLastError());
                        globals::running = false;
                        return TransitionToError { false, 0 };
                    }
                    UnloadTrampoline();
                    continue;
                }
            }
        }
        return TransitionToError { false, 0};
    }

    void DebuggerPhase::Exit() {}

    Transitions DebuggerPhase::ResumeProgram() {
        DEBUG_EVENT de;
        bool debug_event_success;

        ResumeProcess();
        while (globals::running) {
            debug_event_success = WaitForDebugEvent(&de, INFINITE);

            if (debug_event_success) {
                DWORD continue_status = DBG_CONTINUE;

                if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                    const EXCEPTION_RECORD& er = de.u.Exception.ExceptionRecord;

                    if (er.ExceptionCode == STATUS_BREAKPOINT) {
                        DWORD thread_id = de.dwThreadId;
                        uintptr_t rip_address = GetRipAddress(thread_id) - 1;
                        auto bp_type = GetBreakpointType(rip_address, thread_id);

                        switch (bp_type) {
                            // Permanent breakpoints just need to run the trampoline and continue
                            case BreakpointType::Permanent: {
                                RedirectToTrampoline(rip_address, thread_id);
                                break;
                            }
                        }
                    };
                };

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );
            } else {
                DWORD last_error = GetLastError();
                printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                return TransitionToError {false, last_error};
            }
        }     
    }
}

