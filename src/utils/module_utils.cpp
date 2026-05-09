#include "module_utils.h"
#include "globals.h"
#include "nt_undocumented.h"
#include <vector>

namespace chronoporia {

    std::wstring GetDllNameFromLp(LPVOID lp_image_name) {
        // Needs to be a wide string since the result is Unicode (or ansi but I don't have that case
        // supported yet)
        std::wstring image_name {};
        image_name.resize(260);
        size_t bytes_read {};
        if (lp_image_name != nullptr) {
            // lpImageName is a pointer in process space to a pointer where the name is stored
            void *image_name_address;
            ReadProcessMemory(globals::process_handle, lp_image_name, &image_name_address, 8 , nullptr);
            if (image_name_address != nullptr) {
                // read 260 bytes since Windows has a max path limit of 260 characters
                ReadProcessMemory(globals::process_handle, image_name_address, image_name.data(), 260, &bytes_read);
            }
        }
        return image_name;
    }

    std::wstring GetDllNameFromHandle(HANDLE dll_handle) {
        ULONG size = 1024;
        auto buffer = std::vector<std::byte>(size);

        NtQueryObject(dll_handle, static_cast<::OBJECT_INFORMATION_CLASS>(ObjectNameInformation),
            buffer.data(), size, &size);
        
        auto nameInfo = (OBJECT_NAME_INFORMATION*)buffer.data();
        
        return std::wstring{ nameInfo->Name.Buffer,
                            static_cast<size_t>(nameInfo->Name.Length / sizeof(wchar_t)) };
    }
}