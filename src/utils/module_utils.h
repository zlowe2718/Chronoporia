#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>

namespace chronoporia {

    std::wstring GetDllNameFromLp(LPVOID lpImageName);
    std::wstring GetDllNameFromHandle(HANDLE dll_handle);
}