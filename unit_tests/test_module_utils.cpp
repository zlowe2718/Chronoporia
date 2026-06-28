#include <catch2/catch_test_macros.hpp>
#include "module_utils.h"
#include "globals.h"
#include <array>
#include <string>

using namespace chronoporia;

namespace {

// module_utils.cpp reads through globals::process_handle, so pointing it at our own process
// lets these tests exercise the real ReadProcessMemory/NtQueryObject calls without needing an
// actual remote target.
struct SelfProcessFixture {
    SelfProcessFixture() { globals::process_handle = GetCurrentProcess(); }
    ~SelfProcessFixture() { globals::process_handle = 0; }
};

}

TEST_CASE_METHOD(SelfProcessFixture, "GetDllNameFromLp returns an empty (zero-filled) buffer when given nullptr", "[module_utils]") {
    std::wstring name = GetDllNameFromLp(nullptr);

    REQUIRE(name.size() == 260);
    REQUIRE(name.find_first_not_of(L'\0') == std::wstring::npos);
}

TEST_CASE_METHOD(SelfProcessFixture, "GetDllNameFromLp follows the pointer-to-pointer indirection to read the name", "[module_utils]") {
    // lp_image_name points at a location holding the *address* of the actual name string,
    // matching how the debug API hands back LOAD_DLL_DEBUG_INFO::lpImageName.
    std::array<wchar_t, 200> name_buffer {};
    std::wstring expected = L"test_module.dll";
    std::copy(expected.begin(), expected.end(), name_buffer.begin());

    void *name_buffer_address = name_buffer.data();
    void *lp_image_name = &name_buffer_address;

    std::wstring result = GetDllNameFromLp(lp_image_name);

    REQUIRE(result.substr(0, expected.size()) == expected);
}

TEST_CASE_METHOD(SelfProcessFixture, "GetDllNameFromHandle resolves the NT object name behind a real handle", "[module_utils]") {
    wchar_t temp_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);
    std::wstring file_path = std::wstring(temp_path) + L"chronoporia_module_utils_test.tmp";

    HANDLE file_handle = CreateFileW(
        file_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    REQUIRE(file_handle != INVALID_HANDLE_VALUE);

    std::wstring object_name = GetDllNameFromHandle(file_handle);

    // The NT object name is reported as a device-relative path, but it still ends with the
    // file name we created.
    REQUIRE(object_name.size() >= file_path.size());
    REQUIRE(object_name.ends_with(L"chronoporia_module_utils_test.tmp"));

    CloseHandle(file_handle);
    DeleteFileW(file_path.c_str());
}
