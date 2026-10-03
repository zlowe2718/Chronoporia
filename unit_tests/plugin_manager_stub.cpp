// Stub for the plugin_manager symbols that breakpoint_manager.cpp references.
//
// The real definitions live in src/managers/plugin_manager.cpp, which performs
// live plugin DLL loading and relies on the UNICODE character set. The
// unit_tests target intentionally builds without UNICODE (see the root
// CMakeLists.txt comment), so we can't link the real translation unit. Unit
// tests don't exercise real plugin address translation, so this stub returns
// the address unchanged.

#include <cstdint>
#include <string>

namespace chronoporia {
    uintptr_t get_true_address(const std::string & /*plugin_name*/, const uintptr_t code_address) {
        return code_address;
    }
}
