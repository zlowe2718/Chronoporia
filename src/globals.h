#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <map>

namespace globals {
    extern bool running;
    extern DWORD process_id;
    extern HANDLE process_handle;
    extern size_t snapshot_interval;
    extern const void *kUserSharedDataBaseAddress;
    extern const void *kUserSharedDataEndAddress;
    extern uint64_t global_sequence;
    extern std::map<DWORD, uint64_t> thread_id_to_sequence;
    extern DWORD main_thread_id;
}