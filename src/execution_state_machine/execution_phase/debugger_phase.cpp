#include "debugger_phase.h"
#include "error_transition.h"
#include "globals.h"
#include "nt_wrappers.h"
#include "quill/LogMacros.h"
#include "snapshot_log.h"
#include "thread_utils.h"
#include "time_restore_transition.h"
#include "trampoline.h"
#include "trampoline_manager.h"
#include "breakpoint_manager.h"
#include <argparse/argparse.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


namespace {

    std::string UserInput(const std::string& prompt) {
        std::string result;
        printf("%s", prompt.c_str());
        std::getline(std::cin, result);
        return result;
    }

    // Splits input on whitespace into argv-style tokens for argparse.
    std::vector<std::string> SplitInput(const std::string& input) {
        std::vector<std::string> args;
        std::istringstream iss(input);
        std::string token;
        while (iss >> token)
            args.push_back(token);
        return args;
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
                globals::logger->flush_log();
                std::string input = UserInput("chronoporia> ");

                auto args = SplitInput(input);
                if (args.empty()) {
                    printf("Bad string supplied\n");
                    continue;
                }

                const auto& command = args[0];

                if (command == "snapshot") {
                    argparse::ArgumentParser parser("snapshot");
                    parser.add_argument("--list").flag();
                    parser.add_argument("--run_id").default_value(std::string{});
                    parser.add_argument("--run_seq").default_value(std::string{});

                    try {
                        parser.parse_args(args);
                    } catch (const std::exception& e) {
                        printf("snapshot: %s\n", e.what());
                        continue;
                    }

                    if (parser.get<bool>("--list")) {
                        PrintSnapshotHistory();
                        continue;
                    }

                    auto run_id_str  = parser.get<std::string>("--run_id");
                    auto run_seq_str = parser.get<std::string>("--run_seq");

                    if (run_id_str.empty() || run_seq_str.empty()) {
                        printf("Please supply both a run_id and run_seq for the snapshot\n");
                        continue;
                    }

                    uint32_t run_id  = static_cast<uint32_t>(std::stoul(run_id_str));
                    uint32_t run_seq = static_cast<uint32_t>(std::stoul(run_seq_str));

                    if (!ValidSnapshotIdSeq(run_id, run_seq)) {
                        printf("Please supply a valid run_id and run_seq for the snapshot.  Run snapshot --list to see available snapshots\n");
                        continue;  
                    }

                    // guaranteed by the valid check above
                    auto target_global_sequence = GetGlobalSequence(run_id, run_seq);
                    // TODO: Fix this later with a better way to transition back to the debugger
                    return TransitionToTimeRestore {{true}, run_id, run_seq, target_global_sequence.value(),TransitionsBox{Transitions{TransitionToDebugger {true}}}};

                } else if (command == "resume") {
                    ResumeProgram();
                } else if (command == "detach") {
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
