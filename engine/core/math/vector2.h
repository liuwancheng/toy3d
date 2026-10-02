#pragma once

#include "math/scalar_math.h"

#include <cassert>
#include <type_traits>

namespace toy3d
{
    struct Vector2
    {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vector2() = default;
        explicit constexpr Vector2(float value) : x(value), y(value)
        {
        }
        constexpr Vector2(float x_value, float y_value) : x(x_value), y(y_value)
        {
        }

        constexpr float* data()
        {
            return &x;
        }

        constexpr const float* data() const
        {
            return &x;
        }

        constexpr Vector2& operator+=(const Vector2& other)
        {
            x += other.x;
            y += other.y;
            return *this;
        }

        constexpr Vector2& operator-=(const Vector2& other)
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }

        constexpr Vector2& operator*=(const Vector2& other)
        {
            x *= other.x;
            y *= other.y;
            return *this;
        }

        constexpr Vector2& operator/=(const Vector2& other)
        {
            x /= other.x;
            y /= other.y;
            return *this;
        }

        constexpr Vector2& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        constexpr Vector2& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;
            return *this;
        }
    };

    static_assert(sizeof(Vector2) == sizeof(float) * 2, "Vector2 must contain exactly two contiguous floats.");
    static_assert(alignof(Vector2) == alignof(float), "Vector2 must retain scalar alignment.");
    static_assert(std::is_standard_layout<Vector2>::value, "Vector2 must be standard-layout.");
    static_assert(std::is_trivially_copyable<Vector2>::value, "Vector2 must be trivially copyable.");

    constexpr Vector2 operator+(Vector2 value)
    {
        return value;
    }

    constexpr Vector2 operator-(const Vector2& value)
    {
        return Vector2(-value.x, -value.y);
    }

    constexpr Vector2 operator+(Vector2 left, const Vector2& right)
    {
        return left += right;
    }

    constexpr Vector2 operator-(Vector2 left, const Vector2& right)
    {
        return left -= right;
    }

    constexpr Vector2 operator*(Vector2 left, const Vector2& right)
    {
        return left *= right;
    }

    constexpr Vector2 operator/(Vector2 left, const Vector2& right)
    {
        return left /= right;
    }

    constexpr Vector2 operator*(Vector2 value, float scalar)
    {
        return value *= scalar;
    }

    constexpr Vector2 operator*(float scalar, Vector2 value)
    {
        return value *= scalar;
    }

    constexpr Vector2 operator/(Vector2 value, float scalar)
    {
        return value /= scalar;
    }

    constexpr bool operator==(const Vector2& left, const Vector2& right)
    {
        return left.x == right.x && left.y == right.y;
    }

    constexpr bool operator!=(const Vector2& left, const Vector2& right)
    {
        return !(left == right);
    }

    constexpr float dot(const Vector2& left, const Vector2& right)
    {
        return left.x * right.x + left.y * right.y;
    }

    constexpr float length_squared(const Vector2& value)
    {
        return dot(value, value);
    }

    inline float length(const Vector2& value)
    {
        return sqrt(length_squared(value));
    }

    constexpr float distance_squared(const Vector2& left, const Vector2& right)
    {
        return length_squared(right - left);
    }

    inline float distance(const Vector2& left, const Vector2& right)
    {
        return sqrt(distance_squared(left, right));
    }

    inline bool is_finite(const Vector2& value)
    {
        return is_finite(value.x) && is_finite(value.y);
    }

    inline bool is_nearly_equal(const Vector2& left, const Vector2& right, float tolerance = k_default_float_tolerance)
    {
        return is_nearly_equal(left.x, right.x, tolerance) && is_nearly_equal(left.y, right.y, tolerance);
    }

    inline bool try_normalize(const Vector2& value, Vector2& result)
    {
        const float value_length_squared = length_squared(value);
        if (!is_finite(value) || !is_finite(value_length_squared) ||
            value_length_squared <= k_normalization_tolerance_squared)
        {
            return false;
        }

        const Vector2 normalized = value * inverse_sqrt(value_length_squared);
        if (!is_finite(normalized))
        {
            return false;
        }
        result = normalized;
        return true;
    }

    inline Vector2 normalized_or_zero(const Vector2& value)
    {
        Vector2 result;
        try_normalize(value, result);
        return result;
    }

    inline Vector2 normalize_unchecked(const Vector2& value)
    {
        const float value_length_squared = length_squared(value);
        assert(is_finite(value) && is_finite(value_length_squared) &&
               value_length_squared > k_normalization_tolerance_squared);
        return value * inverse_sqrt(value_length_squared);
    }
} // namespace toy3d
