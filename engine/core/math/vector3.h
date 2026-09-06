#pragma once

#include "math/scalar_math.h"

#include <cassert>
#include <type_traits>

namespace toy3d
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vector3() = default;
        explicit constexpr Vector3(float value) : x(value), y(value), z(value) {}
        constexpr Vector3(float x_value, float y_value, float z_value) : x(x_value), y(y_value), z(z_value) {}

        constexpr float* data() { return &x; }

        constexpr const float* data() const { return &x; }

        constexpr Vector3& operator+=(const Vector3& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }

        constexpr Vector3& operator-=(const Vector3& other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            return *this;
        }

        constexpr Vector3& operator*=(const Vector3& other)
        {
            x *= other.x;
            y *= other.y;
            z *= other.z;
            return *this;
        }

        constexpr Vector3& operator/=(const Vector3& other)
        {
            x /= other.x;
            y /= other.y;
            z /= other.z;
            return *this;
        }

        constexpr Vector3& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }

        constexpr Vector3& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            return *this;
        }
    };

    static_assert(sizeof(Vector3) == sizeof(float) * 3, "Vector3 must contain exactly three contiguous floats.");
    static_assert(alignof(Vector3) == alignof(float), "Vector3 must retain scalar alignment.");
    static_assert(std::is_standard_layout<Vector3>::value, "Vector3 must be standard-layout.");
    static_assert(std::is_trivially_copyable<Vector3>::value, "Vector3 must be trivially copyable.");

    constexpr Vector3 operator+(Vector3 value)
    {
        return value;
    }

    constexpr Vector3 operator-(const Vector3& value)
    {
        return Vector3(-value.x, -value.y, -value.z);
    }

    constexpr Vector3 operator+(Vector3 left, const Vector3& right)
    {
        return left += right;
    }

    constexpr Vector3 operator-(Vector3 left, const Vector3& right)
    {
        return left -= right;
    }

    constexpr Vector3 operator*(Vector3 left, const Vector3& right)
    {
        return left *= right;
    }

    constexpr Vector3 operator/(Vector3 left, const Vector3& right)
    {
        return left /= right;
    }

    constexpr Vector3 operator*(Vector3 value, float scalar)
    {
        return value *= scalar;
    }

    constexpr Vector3 operator*(float scalar, Vector3 value)
    {
        return value *= scalar;
    }

    constexpr Vector3 operator/(Vector3 value, float scalar)
    {
        return value /= scalar;
    }

    constexpr bool operator==(const Vector3& left, const Vector3& right)
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    constexpr bool operator!=(const Vector3& left, const Vector3& right)
    {
        return !(left == right);
    }

    constexpr float dot(const Vector3& left, const Vector3& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    constexpr Vector3 cross(const Vector3& left, const Vector3& right)
    {
        return Vector3(left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
                       left.x * right.y - left.y * right.x);
    }

    constexpr float length_squared(const Vector3& value)
    {
        return dot(value, value);
    }

    inline float length(const Vector3& value)
    {
        return sqrt(length_squared(value));
    }

    constexpr float distance_squared(const Vector3& left, const Vector3& right)
    {
        return length_squared(right - left);
    }

    inline float distance(const Vector3& left, const Vector3& right)
    {
        return sqrt(distance_squared(left, right));
    }

    constexpr Vector3 min(const Vector3& left, const Vector3& right)
    {
        return Vector3(min(left.x, right.x), min(left.y, right.y), min(left.z, right.z));
    }

    constexpr Vector3 max(const Vector3& left, const Vector3& right)
    {
        return Vector3(max(left.x, right.x), max(left.y, right.y), max(left.z, right.z));
    }

    inline bool is_finite(const Vector3& value)
    {
        return is_finite(value.x) && is_finite(value.y) && is_finite(value.z);
    }

    inline bool is_nearly_equal(const Vector3& left, const Vector3& right, float tolerance = k_default_float_tolerance)
    {
        return is_nearly_equal(left.x, right.x, tolerance) && is_nearly_equal(left.y, right.y, tolerance) &&
               is_nearly_equal(left.z, right.z, tolerance);
    }

    inline bool try_normalize(const Vector3& value, Vector3& result)
    {
        const float value_length_squared = length_squared(value);
        if (!is_finite(value) || !is_finite(value_length_squared) ||
            value_length_squared <= k_normalization_tolerance_squared)
        {
            return false;
        }

        const Vector3 normalized = value * inverse_sqrt(value_length_squared);
        if (!is_finite(normalized))
        {
            return false;
        }
        result = normalized;
        return true;
    }

    inline Vector3 normalized_or_zero(const Vector3& value)
    {
        Vector3 result;
        try_normalize(value, result);
        return result;
    }

    inline Vector3 normalize_unchecked(const Vector3& value)
    {
        const float value_length_squared = length_squared(value);
        assert(is_finite(value) && is_finite(value_length_squared) &&
               value_length_squared > k_normalization_tolerance_squared);
        return value * inverse_sqrt(value_length_squared);
    }
} // namespace toy3d
