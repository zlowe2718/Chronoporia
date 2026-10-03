#pragma once

#include "base_breakpoint_plugin.h"
#include "dia2.h"
#include <combaseapi.h>
#include <atlcomcli.h>
#include <cstdint>
#include <optional>
#include <vector>

// All of the following functions should take in the original code address not the true address (if remapped)
// The conversion between original and true addresses should take place outside these functions

namespace breakpoint_plugin {
    // Primitive types a resolved variable can carry across the flat int64_t argument
    // slots passed to a compiled condition. TODO: extend to user-defined structs once we
    // can walk DIA UDT member layouts (name/offset/type) and regenerate matching field
    // access in the synthesized source instead of relying on primitive bit-packing.
    enum class VariableKind {
        Char, WChar, Bool, Int8, Int16, Int32, Int64, Float, Double, Pointer,
        UInt8, UInt16, UInt32, UInt64,
    };

    // Where to read a variable's live value from when a breakpoint is hit.
    struct VariableLocation {
        enum class Kind { Register, RegisterRelative, Static, Optimized } kind;
        DWORD register_id = 0;      // CV_REG_* / CV_AMD64_* from cvconst.h
        int64_t offset = 0;         // valid when kind == RegisterRelative
        OriginalAddress static_address {0}; // valid when kind == Static
    };

    struct VariableInfo {
        std::string name;
        std::string cpp_type; // e.g. "int", "double", "MyEnum*"
        VariableKind kind;
        VariableLocation location;
    };

    struct VariableType {
        VariableKind kind;
        std::string cpp_type;
    };

    struct FunctionRange {
        OriginalAddress start;
        OriginalAddress end;
        std::string name;
    };

    // LineRange is [start, end).  End is the next instruction after the line
    struct LineRange {
        OriginalAddress start;
        OriginalAddress end;
    };

    bool load_pdb(const std::wstring &path);
    void unload_pdb();

    SourceLocation load_breakpoint_from_pdb(const OriginalAddress original_address, const uintptr_t image_base);

    // Classifies a DIA data symbol's type into the primitive VariableKind set the codegen
    // understands, along with the C++ spelling to use for casts/locals in generated source.
    // TODO: once struct/UDT support lands, this should recurse into SymTagUDT member layouts
    // instead of bailing out to VariableKind::Pointer-as-opaque-address for everything else.
    std::optional<VariableType> resolve_variable_type(IDiaSymbol *type_symbol);

    // TODO: only enumerates the function's immediate SymTagData children, so locals
    // declared inside nested lexical blocks (if/for/while bodies) won't be found. Needs
    // to recurse into SymTagBlock children of the function symbol as well.
    // TODO: struct/class-typed locals and parameters are silently skipped (ClassifyVariableType
    // returns false for anything but base types and pointers). Support them by walking DIA's
    // SymTagUDT member list (name/offset/type per field) and generating matching field access
    // in RenderConditionSource instead of flattening everything to int64_t slots.
    std::vector<VariableInfo> resolve_variables(const OriginalAddress original_address, const uintptr_t image_base);

    OriginalAddress get_address_from_func_name(const std::string& name, const uintptr_t image_base);
    OriginalAddress get_address_from_c_label(const std::string& name, const uintptr_t image_base);
    OriginalAddress get_address_from_file_line(const std::string& name, const uintptr_t image_base);

    FunctionBounds get_function_bounds_from_address(const OriginalAddress original_address, const uintptr_t image_base);
    std::vector<LineRange> get_line_ranges_from_address(const OriginalAddress original_address, const uintptr_t image_base);

    void print_all_pdb_scoped_variables(const OriginalAddress original_address, const DWORD thread_id, const uintptr_t image_base, HANDLE process_handle);
    void print_locals(const OriginalAddress original_address, const CONTEXT& ctx, const uintptr_t image_base, HANDLE process_handle);
    void print_globals(const OriginalAddress original_address, const CONTEXT& ctx, const uintptr_t image_base, HANDLE process_handle);
    void print_object_from_type(const std::string& object_type, const std::string& pdb_file, const uintptr_t data_address);

    // Packs the incoming arguments into an int64_t array so we can evaluate the compiled conditional expression
    std::vector<int64_t> pack_incoming_args(const std::vector<VariableInfo> &vars, DWORD thread_id, HANDLE process_handle);

    uint64_t read_value_from_kind(const VariableLocation& loc, const CONTEXT& ctx, HANDLE process_handle);

    std::vector<FunctionRange> get_all_functions(const uintptr_t image_base);
    bool address_in_line_ranges(const std::vector<LineRange> &line_ranges, const OriginalAddress address);
}