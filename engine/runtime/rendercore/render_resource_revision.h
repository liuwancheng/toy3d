#pragma once

#include <cstdint>

namespace toy3d
{
    class RenderResourceRevision
    {
    public:
        using ValueType = std::uint64_t;

        constexpr RenderResourceRevision() = default;
        explicit constexpr RenderResourceRevision(ValueType value) : value_(value) {}

        constexpr ValueType value() const { return value_; }
        explicit constexpr operator bool() const { return value_ != 0; }

        friend constexpr bool operator==(
            RenderResourceRevision lhs,
            RenderResourceRevision rhs)
        {
            return lhs.value_ == rhs.value_;
        }

        friend constexpr bool operator!=(
            RenderResourceRevision lhs,
            RenderResourceRevision rhs)
        {
            return !(lhs == rhs);
        }

        friend constexpr bool operator<(
            RenderResourceRevision lhs,
            RenderResourceRevision rhs)
        {
            return lhs.value_ < rhs.value_;
        }

    private:
        ValueType value_ = 0;
    };
}
