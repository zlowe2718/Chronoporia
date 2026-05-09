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

    // Need the LdrLoadDll parameters 
    // TODO: maybe I also create a thread in the process that's only purposes is to do this instead of creating and deleting one every time
    // This involves creating a thread in the process then having that thread call LdrLoadDll
    // void ForceLoadDLLAtAddress(const void *base_address);
}