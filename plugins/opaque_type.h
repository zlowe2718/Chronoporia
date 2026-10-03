#pragma once
#include <cstdint>
#include <compare>

namespace breakpoint_plugin {
    template<typename Tag>
    class OpaqueAddress {
    public:
        OpaqueAddress() : val {} {}
        explicit OpaqueAddress(const uintptr_t value) : val {value} {}

        bool operator==(const OpaqueAddress& val_2) const = default;
        auto operator<=>(const OpaqueAddress& val_2) const = default;

        bool operator==(const uintptr_t val_2) const { return val == val_2; }
        auto operator<=>(const uintptr_t val_2) const { return val <=> val_2; }

        uintptr_t get() const { return val; }
    private:
        uintptr_t val;
    };

    static_assert(sizeof(OpaqueAddress<struct SelfTest>) == sizeof(uintptr_t));
    static_assert(std::is_trivially_copyable_v<OpaqueAddress<struct SelfTest>>);
}

namespace std {
    template<typename Tag>
    struct hash<breakpoint_plugin::OpaqueAddress<Tag>> {
        size_t operator()(const breakpoint_plugin::OpaqueAddress<Tag>& addr) const noexcept {
            return hash<uintptr_t>{}(addr.get());
        }
    };
}