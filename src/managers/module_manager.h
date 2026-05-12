#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <vector>

namespace chronoporia {

    struct DllInfo {
        HMODULE dll_handle;
        std::wstring dll_name;
        bool loaded;
    };

    void TrackDLL(HMODULE dll_handle, std::wstring dll_name, uint64_t global_seq);
    void UntrackDLL(HMODULE dll_handle, uint64_t global_seq);

    void TrackAllCurrentDLLs(uint64_t global_seq);

    std::vector<DllInfo> RestoreDLLsAtSequence(uint64_t global_seq);
}