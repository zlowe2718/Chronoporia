#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <array>
#include "globals.h"

namespace chronoporia {
    // struct BaseEvent {
    //     uint64_t global_seq;
    //     uint64_t thread_seq;
    //     uint64_t instruction_count;

    //     DWORD thread_id;
    // };

    class BaseEvent {
    public:
        uint64_t global_seq;
        uint64_t thread_seq;
        DWORD thread_id;

        ~BaseEvent() = default;

        BaseEvent(DWORD thread_id)
            : thread_id{thread_id}
            {
                global_seq = globals::global_sequence;
                thread_seq = globals::thread_id_to_sequence[thread_id];

                globals::global_sequence += 1;
                globals::thread_id_to_sequence[thread_id] += 1;
            }

        virtual void FinishEvent(const CONTEXT& thread_ctx) = 0;
        virtual void apply() {};

        template <typename StackArgType>
        void ReadStackArgs(const uintptr_t stack_address, StackArgType& stack_args) {
            uint64_t total_read_size = sizeof(StackArgType);

            std::array<StackArgType, 1> buffer;

            SIZE_T bytes_read;
            if (!ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(stack_address), buffer.data(), total_read_size, &bytes_read)) {
                printf("Read Process Memory failed at address %p\n"
                        " error: %ld, bytes_read: %lld\n", stack_address, GetLastError(), bytes_read);
            }

            // Doing a memcpy once so I don't do a memcpy or ReadProcessMemory for each stack arg
            memcpy(&stack_args, buffer.data(), sizeof(StackArgType));
        }
    };
}