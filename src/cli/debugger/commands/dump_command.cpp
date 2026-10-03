#include "dump_command.h"
#include "breakpoint_manager.h"
#include "globals.h"
#include "plugin_manager.h"
#include "process_utils.h"
#include "thread_utils.h"
#include "instruction_utils.h"
#include <optional>

namespace chronoporia {

    // TODO: handle the original/true address switch
    // TODO: add print multiple instructions (like lines)

    std::optional<Transitions> DumpCommand::handle_command() {
        if (dump_command.get<bool>("--variables")) {
            uint64_t rip = GetRipAddress(globals::main_thread_id);
            print_all_scoped_variables(rip, globals::main_thread_id);
            return std::nullopt;
        }

        if (dump_command.get<bool>("--instruction")) {
            uintptr_t current_address = GetRipAddress(globals::main_thread_id);
            auto bp_type = GetBreakpointType(current_address, globals::main_thread_id);

            if (!bp_type) {
                print_instruction_at_address(current_address);
                return std::nullopt;                
            }

            // TODO: Handle the other breakpoint types
            if (bp_type == BreakpointType::User) {
                disable_user_breakpoint(current_address);
                print_instruction_at_address(current_address);
                enable_user_breakpoint(current_address);
                return std::nullopt;
            }

            print_instruction_at_address(current_address);
            return std::nullopt;
        }

        if (dump_command.get<bool>("--address")) {
            // TODO: Need to better track when I need to subtract one and when I don't
            // No subtract one for now since I'm assuming a resume command
            uintptr_t current_address = GetRipAddress(globals::main_thread_id);
            printf("%p\n", reinterpret_cast<void *>(current_address));
            return std::nullopt;
        }

        if (dump_command.is_used("--stack-frames")) {
            auto thread_id = dump_command.get<uint32_t>("--stack-frames");
            print_stack("cpp", thread_id);
            return std::nullopt;
        }

        if (dump_command.is_used("--register")) {
            auto thread_id = dump_command.get<uint32_t>("--register");
            print_registers("cpp", thread_id);
            return std::nullopt;
        }

        if (dump_command.is_used("--lines")) {
            auto lines = dump_command.get<std::vector<int16_t>>("--lines");
            if (lines.size() == 2 && lines[1] <= lines[0]) {
                printf("Second value must be strictly greater than the first value\n");
                return std::nullopt;
            }
            const uintptr_t current_address = get_original_address("cpp", GetRipAddress(globals::main_thread_id));
            print_lines("cpp", current_address, lines);
            return std::nullopt;
        }

        if (dump_command.get<bool>("--peb")) {
            print_peb();
            return std::nullopt;
        }

        if (dump_command.is_used("--teb")) {
            auto thread_id = dump_command.get<uint32_t>("--teb");
            print_teb(thread_id);
            return std::nullopt;
        }

        if (dump_command.is_used("--handle")) {
            auto handle_value = dump_command.get<uint64_t>("--handle");
            print_handle_information(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle_value)));
            return std::nullopt;
        }

        if (dump_command.is_used("--display-type")) {
            std::string object_type = dump_command.get<std::string>("--display-type");
            print_object_type("cpp", object_type, std::string {}, 0);
            return std::nullopt;
        }

        if (dump_command.is_used("--tls")) {
            auto tls_index = dump_command.get<int64_t>("--tls");
            print_tls_information(tls_index, globals::main_thread_id);
            return std::nullopt;           
        }

        printf("%s\n", dump_command.help().str().c_str());
        return std::nullopt;
    }

}