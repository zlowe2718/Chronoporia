#pragma once
#include "base_event.h"
#include <memory>

namespace chronoporia {

    class CoarseSnapshotEvent : public BaseEvent {
    public:
        CoarseSnapshotEvent(DWORD thread_id) : BaseEvent(thread_id) {};
        void FinishEvent([[maybe_unused]] const CONTEXT& thread_ctx) override {};
        void apply() override;
    };

    class LineSnapshotEvent : public BaseEvent {
    public:
        // TODO: add file name or something
        uint32_t line;
        
        LineSnapshotEvent(DWORD thread_id)
            : BaseEvent(thread_id)
            {};
        void FinishEvent([[maybe_unused]] const CONTEXT& thread_ctx) override {};
        void apply() override {};
    };

    std::unique_ptr<CoarseSnapshotEvent> CreateCoarseSnapshotEvent(const DWORD thread_id, bool only_dirty_pages);
}