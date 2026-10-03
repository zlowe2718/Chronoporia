#pragma once

#include "pdb.h"
#include <string>
#include <vector>

namespace breakpoint_plugin {
    struct CompiledCondition {
        HMODULE module = nullptr;
        bool (__cdecl *evaluate)(const int64_t *) = nullptr;
        std::vector<VariableInfo> variables;
    };

    // Compile the created conditional expression into a dll
    bool compile_condition(const std::string &source, const std::wstring &output_dll_path);

    // TODO: string conditions like `x == "test_string"` don't work correctly yet for
    // char*/wchar_t* variables -- naive `==` in the generated source compares pointers,
    // not contents. Need to detect string-typed operands and rewrite comparisons to
    // strcmp/wcscmp (or std::string_view equality) when rendering the condition source.
    std::string build_conditional_source_code(const std::string &expression, const std::vector<VariableInfo> &vars);
}