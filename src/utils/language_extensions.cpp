#include "language_extensions.h"
#include <optional>
#include <unordered_map>

namespace {

    std::unordered_map<std::string, std::string> extension_map {
        {"cpp", "cpp"},
        {"c++", "cpp"},
        {"cxx", "cpp"},
        {"cc", "cpp"},
        {"C", "cpp"},
        {"c", "c"},       
        {"py", "python"},
    };

}

namespace chronoporia {

    std::optional<std::string> get_language_from_ext(const std::string &extension) {
        if (extension_map.contains(extension)) {
            return extension_map[extension];
        } 
        return std::nullopt;
    }

}