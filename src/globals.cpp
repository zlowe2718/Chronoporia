#include "globals.h"

namespace globals {
    bool running {false};
    DWORD process_id {0};
    HANDLE process_handle {0};
    size_t snapshot_interval {5000};

    const void *kUserSharedDataBaseAddress {reinterpret_cast<void *>(0x7ffe0000)};
    const void *kUserSharedDataEndAddress {reinterpret_cast<void *>(0x7ffeffff)};

    uint64_t global_sequence {0};
    uint32_t run_id {0};
    uint32_t run_sequence {0};
    std::map<DWORD, uint64_t> thread_id_to_sequence {};
    DWORD main_thread_id {0};
}