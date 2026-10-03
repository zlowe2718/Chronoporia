#include "misc.h"
#include <string>
#include <vector>
#include <locale>
#include <ranges>

namespace chronoporia {

    std::string wstring_to_string(const std::wstring& wstr) {
        if (wstr.empty()) return {};
        
        // Use the default system locale
        std::locale loc(""); 
        
        std::vector<char> buffer(wstr.size());
        std::use_facet<std::ctype<wchar_t>>(loc).narrow(
            wstr.data(), 
            wstr.data() + wstr.size(), 
            '?', // Placeholder for unconvertible characters
            buffer.data()
        );
        
        return std::string(buffer.data(), buffer.size());
    }

    std::vector<std::string> split_string(const std::string& delimited_string, const char& delimiter) {
        return std::views::split(delimited_string, delimiter) 
                | std::ranges::to<std::vector<std::string>>();
    }

}