#include "breakpoint_command.h"
#include "breakpoint_manager.h"
#include "plugin_manager.h"
#include "transition_variant.h"
#include <optional>

namespace chronoporia {

    std::optional<Transitions> BreakpointCommand::handle_command() {
        if (breakpoint_command.get<bool>("--list")) {
            print_all_user_breakpoints();
            return std::nullopt;
        }

        if (breakpoint_command.is_used("--function")) {
            std::string function_name = breakpoint_command.get<std::string>("--function");
            if (function_name.empty()) {
                printf("Please supply a valid function to attach a breakpoint to\n");
                return std::nullopt;                      
            }
            
            uintptr_t function_address = get_address_from_name_type("cpp", function_name, "function");
            std::string condition = breakpoint_command.get<std::string>("--condition");
            if (condition.empty()) {
                printf("Please supply a valid condition to attach a breakpoint to\n");
                return std::nullopt;
            } else {
                if (condition.front() == '"' || condition.front() == '\'') {
                    condition.erase(0, 1);
                }
                if (condition.back() == '"' || condition.back() == '\'') {
                    condition.pop_back();
                }
                bool success = create_conditional_breakpoint("cpp", function_address, condition);
                if (!success) {
                    printf("Condition failed to create\n");
                    return std::nullopt;
                }
            }
            CreateUserBreakpoint(function_address, condition);
            return std::nullopt;
        }

        if (breakpoint_command.is_used("--file")) {
            std::string file_name = breakpoint_command.get<std::string>("--file");
            std::string line = breakpoint_command.get<std::string>("--line");
            
            uintptr_t file_line_address = get_address_from_name_type("cpp", file_name + ":" + line, "file_line");
            std::string condition = breakpoint_command.get<std::string>("--condition");
            if (condition.empty()) {
                printf("Please supply a valid condition to attach a breakpoint to\n");
                return std::nullopt;;
            } else {
                if (condition.front() == '"' || condition.front() == '\'') {
                    condition.erase(0, 1);
                }
                if (condition.back() == '"' || condition.back() == '\'') {
                    condition.pop_back();
                }
                bool success = create_conditional_breakpoint("cpp", file_line_address, condition);
                if (!success) {
                    printf("Condition failed to create\n");
                    return std::nullopt;
                }
            }
            CreateUserBreakpoint(file_line_address, condition);
            return std::nullopt;
        }

        if (breakpoint_command.is_used("--remove")) {
            std::string remove_breakpoint = breakpoint_command.get<std::string>("--remove");
            if (remove_breakpoint.empty()) {
                printf("Please supply a valid breakpoint to remove.  Run bp --list to see all current breakpoints\n");
                return std::nullopt;                        
            }

            uintptr_t address = static_cast<uintptr_t>(std::stoull(remove_breakpoint, nullptr, 0));
            if (!get_user_breakpoint(address).has_value()) {
                printf("Please supply a valid breakpoint to remove.  Run bp --list to see all current breakpoints\n");
                return std::nullopt;
            }

            bool success = remove_user_breakpoint(address);
            if (!success) {
                printf("Breakpoint %p was not able to be removed\n", reinterpret_cast<void *>(address));
            }
            return std::nullopt;
        }

        if (breakpoint_command.is_used("--address")) {
            std::string add_breakpoint = breakpoint_command.get<std::string>("--address");
            if (add_breakpoint.empty()) {
                printf("Please supply a valid breakpoint to add\n");
                return std::nullopt;                          
            }

            // TODO: handle bad values
            uintptr_t address = static_cast<uintptr_t>(std::stoull(add_breakpoint, nullptr, 0));
            CreateUserBreakpoint(address, "");
            return std::nullopt;
        }

        printf("%s\n", breakpoint_command.help().str().c_str());
        return std::nullopt;
    }
}