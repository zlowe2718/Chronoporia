#pragma once
#include "quill/Logger.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <map>

namespace globals {
    extern bool running;
    extern DWORD process_id;
    extern HANDLE process_handle;
    // interval time between coarse snapshots in milliseconds
    extern size_t snapshot_interval;
    extern const void *kUserSharedDataBaseAddress;
    extern const void *kUserSharedDataEndAddress;
    extern uint64_t global_sequence;
    extern uint32_t run_id;
    extern uint32_t run_sequence;
    extern std::map<DWORD, uint64_t> thread_id_to_sequence;
    extern DWORD main_thread_id;
    extern quill::Logger* logger;
}