#include "globals.h"

namespace globals {
    bool running {false};
    DWORD process_id {0};
    HANDLE process_handle {0};
    size_t snapshot_interval {5000};

    const void *kUserSharedDataBaseAddress {reinterpret_cast<void *>(0x7ffe0000)};
    const void *kUserSharedDataEndAddress {reinterpret_cast<void *>(0x7ffeffff)};
    const uint64_t kTargetSentinel {static_cast<uint64_t>(-1)};

    uint64_t global_sequence {0};
    uint32_t run_id {0};
    uint32_t run_sequence {0};

    std::map<DWORD, uint64_t> thread_id_to_sequence {};
    DWORD main_thread_id {0};

    std::string current_debugger_language = "cpp";

    quill::Logger* logger {nullptr};
}