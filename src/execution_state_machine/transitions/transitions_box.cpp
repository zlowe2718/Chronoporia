#include "transitions_box.h"
#include "transition_variant.h"

namespace chronoporia {

    TransitionsBox::TransitionsBox() = default;
    TransitionsBox::TransitionsBox(Transitions&& t) : value(std::make_unique<Transitions>(std::move(t))) {}
    TransitionsBox::TransitionsBox(TransitionsBox&& other) noexcept = default;
    TransitionsBox& TransitionsBox::operator=(TransitionsBox&& other) noexcept = default;
    TransitionsBox::~TransitionsBox() = default;

}
