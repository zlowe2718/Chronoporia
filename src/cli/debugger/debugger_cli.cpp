#include "debugger_cli.h"
#include <optional>

namespace chronoporia {

    std::optional<Transitions> DebuggerCLI::handle_cli(const std::vector<std::string> &args) {
        if (args.size() == 1) {
            printf("%s\n", debugger_parser_.help().str().c_str());
            return std::nullopt;
        }

        try {
            debugger_parser_.parse_args(args);
        } catch (const std::exception& e) {
            printf("%s\n", e.what());
            printf("%s\n", debugger_parser_.help().str().c_str());
            return std::nullopt;
        }

        if (debugger_parser_.is_subcommand_used("snapshot")) {
           return snapshot_command_.handle_command();
        } else if (debugger_parser_.is_subcommand_used("bp")) {
            return breakpoint_command_.handle_command();
        } else if (debugger_parser_.is_subcommand_used("dump")) {
            return dump_command_.handle_command();
        } else if (debugger_parser_.is_subcommand_used("detach")) {
            return detach_command_.handle_command();
        } else if (debugger_parser_.is_subcommand_used("resume")) {
            return resume_command_.handle_command();
        } else if (debugger_parser_.is_subcommand_used("step")) {
            return step_command_.handle_command();
        }
        return std::nullopt;
    }

}