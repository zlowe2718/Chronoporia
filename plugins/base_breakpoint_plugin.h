#pragma once
#include "opaque_type.h"
#include <Windows.h>
#include <cstdint>
#include <string>

namespace breakpoint_plugin {
    // The relative virtual address.  This is an offset from the image base address
    using RVA = DWORD;

    // This is the exact instruction address as it lives in virtual memory
    using TrueAddress = OpaqueAddress<struct TrueTag>;
    // This is the original instruction address before it may have been remapped
    using OriginalAddress = OpaqueAddress<struct OriginalTag>;

    enum class ProcessInformationType {
        Register,
        AllRegisters,
        ScopedVariables,
        Stack,
        AllStacks,
        Thread, // Dump both register and stack frames?
        AllThreads,
        Lines,
    };

    enum class StepMode {
        Line,
        Into
    };

    enum class ReverseStepMode {
        Instruction,
        Line,
        Into,
    };

    enum class StepBreakpointKind {
        Line, // Next line, we can stop here
        Call, // Call site for step into mode, stop here then step one instruction
        IndirectJump, // Indirect jump, stop here then step one instruction to see where we end up
        Return, // Return statement, stop here then step one instruction
    };

    struct StepBreakpoint {
        uintptr_t original_address;
        uintptr_t true_address;
        StepBreakpointKind step_kind;
        bool has_source_info;
    };

    // need to keep this POD safe so using wchar_t *
    struct SourceLocation {
        wchar_t file_name[MAX_PATH];
        uint64_t line_number;
        uintptr_t code_address;
        bool statement_beginning;
    };

    // TODO: implement
    struct StackFrames {};

    struct FunctionBounds {
        uintptr_t start;
        uintptr_t end;
    };

    struct ReturnAddress {
        uintptr_t original_address;
        bool has_symbols;
    };

    struct PluginMeta {
        char languages[200];
    };

    struct PotentialPredecessor {
        uintptr_t true_address;
        uint64_t branch_counter;
        // A block leader is the first instruction of a region following a branch statement
        // TODO: Do I need this?
        bool is_block_leader;
    };
}