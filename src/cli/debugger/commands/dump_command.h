#pragma once
#include "argparse/argparse.hpp"
#include "cli_command.h"
#include "transition_variant.h"
namespace chronoporia {

    class DumpCommand : public CLICommand {
    public:
        DumpCommand() {};
        DumpCommand(argparse::ArgumentParser& debugger_parser) 
        : dump_command {"dump", "1.0", argparse::default_arguments::all, false} 
        {
            dump_command.add_description("Dump current values for stack, threads, variables etc.");
            dump_command.add_argument("--variables").flag();
            dump_command.add_argument("--address").flag();
            dump_command.add_argument("--instruction").flag();
            dump_command.add_argument("--line").flag();
            dump_command.add_argument("--stack-frames")
                .help("Print stack for a thread or leave empty for all threads")
                .scan<'i', uint32_t>()
                .default_value(uint32_t {0});
            dump_command.add_argument("--register")
                .help("Print the register values for a thread or leave empty for all threads")
                .scan<'i', uint32_t>()
                .default_value(uint32_t {0});
            dump_command.add_argument("--lines")
                .help("Prints X lines below or above including the current executing line.  Negative X will print X lines above including the current line."
                    "Optionally supply Y lines to define a range from X to Y")
                .nargs(1, 2)
                .scan<'i', int16_t>();

            // process information like windbg commands
            dump_command.add_argument("--peb")
                .flag()
                .help("Print the process's Process Environment Block (PEB)");
            dump_command.add_argument("--teb")
                .help("Print the process's Thread Environment Block (TEB). Optionally provide a thread to view, or leave blank to see all TEBs")
                .scan<'i', uint32_t>()
                .default_value(uint32_t {0});
            dump_command.add_argument("--handle")
                .help("Print currently used handles or handle information.  Leave blank to see all handles and their offset.  Provide a handle value in hex (0x1a4 or 1a4) to see more information")
                .scan<'x', uint64_t>()
                .default_value(uint64_t {0});
            dump_command.add_argument("--tls")
                .help("Print the process's TLS slot value.  Leave blank to see all TLS slots")
                .scan<'i', int64_t>()
                .default_value(int64_t {-1});            

            dump_command.add_argument("--display-type")
                .help("print the struct definition.  Usage --dt MyStruct")
                .default_value(std::string {});
            debugger_parser.add_subparser(dump_command);
        }

        std::optional<Transitions> handle_command();
    private:
        argparse::ArgumentParser dump_command;
    };
}