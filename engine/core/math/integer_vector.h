#pragma once

#include <cstdint>
#include <type_traits>

namespace toy3d
{
    struct UIntVector2
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;

        constexpr UIntVector2() = default;
        explicit constexpr UIntVector2(std::uint32_t value) : x(value), y(value) {}
        constexpr UIntVector2(std::uint32_t x_value, std::uint32_t y_value) : x(x_value), y(y_value) {}
        constexpr std::uint32_t* data() { return &x; }
        constexpr const std::uint32_t* data() const { return &x; }
    };

    struct UIntVector3
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t z = 0;

        constexpr UIntVector3() = default;
        explicit constexpr UIntVector3(std::uint32_t value) : x(value), y(value), z(value) {}
        constexpr UIntVector3(std::uint32_t x_value, std::uint32_t y_value, std::uint32_t z_value)
            : x(x_value), y(y_value), z(z_value)
        {
        }
        constexpr std::uint32_t* data() { return &x; }
        constexpr const std::uint32_t* data() const { return &x; }
    };

    struct UIntVector4
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t z = 0;
        std::uint32_t w = 0;

        constexpr UIntVector4() = default;
        explicit constexpr UIntVector4(std::uint32_t value) : x(value), y(value), z(value), w(value) {}
        constexpr UIntVector4(std::uint32_t x_value, std::uint32_t y_value, std::uint32_t z_value,
                              std::uint32_t w_value)
            : x(x_value), y(y_value), z(z_value), w(w_value)
        {
        }
        constexpr std::uint32_t* data() { return &x; }
        constexpr const std::uint32_t* data() const { return &x; }
    };

    constexpr bool operator==(const UIntVector2& left, const UIntVector2& right)
    {
        return left.x == right.x && left.y == right.y;
    }
    constexpr bool operator!=(const UIntVector2& left, const UIntVector2& right)
    {
        return !(left == right);
    }
    constexpr bool operator==(const UIntVector3& left, const UIntVector3& right)
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }
    constexpr bool operator!=(const UIntVector3& left, const UIntVector3& right)
    {
        return !(left == right);
    }
    constexpr bool operator==(const UIntVector4& left, const UIntVector4& right)
    {
        return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
    }
    constexpr bool operator!=(const UIntVector4& left, const UIntVector4& right)
    {
        return !(left == right);
    }

    static_assert(sizeof(UIntVector2) == sizeof(std::uint32_t) * 2,
                  "UIntVector2 must contain exactly two contiguous uint32 values.");
    static_assert(sizeof(UIntVector3) == sizeof(std::uint32_t) * 3,
                  "UIntVector3 must contain exactly three contiguous uint32 values.");
    static_assert(sizeof(UIntVector4) == sizeof(std::uint32_t) * 4,
                  "UIntVector4 must contain exactly four contiguous uint32 values.");
    static_assert(std::is_standard_layout<UIntVector2>::value && std::is_trivially_copyable<UIntVector2>::value,
                  "UIntVector2 must be a simple value type.");
    static_assert(std::is_standard_layout<UIntVector3>::value && std::is_trivially_copyable<UIntVector3>::value,
                  "UIntVector3 must be a simple value type.");
    static_assert(std::is_standard_layout<UIntVector4>::value && std::is_trivially_copyable<UIntVector4>::value,
                  "UIntVector4 must be a simple value type.");
} // namespace toy3d
