#include <string>
#include <vector>

namespace chronoporia {

    std::string wstring_to_string(const std::wstring& wstr);
    std::vector<std::string> split_string(const std::string& delimited_string, const char& delimiter);

}