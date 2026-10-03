#pragma once
#include <memory>

namespace chronoporia {

    struct Transitions;

    // Breaks the circular dependency between TransitionToTimeRestore and the
    // Transitions variant (which itself contains TransitionToTimeRestore) by
    // holding the next transition behind a pointer to an incomplete type.
    struct TransitionsBox {
        TransitionsBox();
        explicit TransitionsBox(Transitions&& t);
        TransitionsBox(TransitionsBox&& other) noexcept;
        TransitionsBox& operator=(TransitionsBox&& other) noexcept;
        ~TransitionsBox();

        std::unique_ptr<Transitions> value;
    };

}
