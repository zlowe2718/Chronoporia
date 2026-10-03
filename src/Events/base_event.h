#pragma once
#include "quill/LogMacros.h"
#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <array>
#include "globals.h"

namespace chronoporia {
    enum class ReplayKind { Stub, Execute };

    class BaseEvent {
    public:
        uint64_t global_seq;
        uint32_t run_id;
        uint32_t run_sequence;
        uint64_t thread_seq;
        DWORD thread_id;
        uintptr_t event_rip;
        ReplayKind replay_kind;
        bool replayed;

        virtual ~BaseEvent() = default;

        BaseEvent(DWORD thread_id, uintptr_t event_rip)
            : thread_id {thread_id}
            , event_rip {event_rip}
            , replay_kind { ReplayKind::Execute }
            , replayed {false}
            {
                global_seq = globals::global_sequence;
                run_id = globals::run_id;
                run_sequence = globals::run_sequence;
                thread_seq = globals::thread_id_to_sequence[thread_id];

                globals::global_sequence += 1;
                globals::thread_id_to_sequence[thread_id] += 1;

                // TODO: not sure if I want this here or if I want to keep run sequence different from event sequence
                globals::run_sequence += 1;
            }

        virtual void FinishEvent(const CONTEXT& thread_ctx) = 0;
        virtual void ReplayEvent() = 0;
        virtual void ReplayEventEnd() {};

        template <typename StackArgType>
        void ReadStackArgs(const uintptr_t stack_address, StackArgType& stack_args) {
            uint64_t total_read_size = sizeof(StackArgType);

            std::array<StackArgType, 1> buffer;

            SIZE_T bytes_read;
            if (!ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(stack_address), buffer.data(), total_read_size, &bytes_read)) {
                LOG_WARNING(globals::logger, "Read Process Memory failed at address {:p}\n"
                            " error: {}, bytes_read: {}", stack_address, GetLastError(), bytes_read);
            }

            // Doing a memcpy once so I don't do a memcpy or ReadProcessMemory for each stack arg
            memcpy(&stack_args, buffer.data(), sizeof(StackArgType));
        }
    };
}