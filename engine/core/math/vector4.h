#pragma once

#include "math/scalar_math.h"
#include "math/vector3.h"

#include <cassert>
#include <type_traits>

namespace toy3d
{
    struct Vector4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        constexpr Vector4() = default;
        explicit constexpr Vector4(float value)
            : x(value), y(value), z(value), w(value)
        {}
        constexpr Vector4(
            float x_value,
            float y_value,
            float z_value,
            float w_value)
            : x(x_value), y(y_value), z(z_value), w(w_value)
        {}
        constexpr Vector4(const Vector3& xyz, float w_value)
            : x(xyz.x), y(xyz.y), z(xyz.z), w(w_value)
        {}

        constexpr float* data() { return &x; }
        constexpr const float* data() const { return &x; }

        constexpr Vector4& operator+=(const Vector4& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            w += other.w;
            return *this;
        }

        constexpr Vector4& operator-=(const Vector4& other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            w -= other.w;
            return *this;
        }

        constexpr Vector4& operator*=(const Vector4& other)
        {
            x *= other.x;
            y *= other.y;
            z *= other.z;
            w *= other.w;
            return *this;
        }

        constexpr Vector4& operator/=(const Vector4& other)
        {
            x /= other.x;
            y /= other.y;
            z /= other.z;
            w /= other.w;
            return *this;
        }

        constexpr Vector4& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            w *= scalar;
            return *this;
        }

        constexpr Vector4& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            w /= scalar;
            return *this;
        }
    };

    static_assert(sizeof(Vector4) == sizeof(float) * 4,
        "Vector4 must contain exactly four contiguous floats.");
    static_assert(alignof(Vector4) == alignof(float),
        "Vector4 must retain scalar alignment.");
    static_assert(std::is_standard_layout<Vector4>::value,
        "Vector4 must be standard-layout.");
    static_assert(std::is_trivially_copyable<Vector4>::value,
        "Vector4 must be trivially copyable.");

    constexpr Vector4 operator+(Vector4 value) { return value; }
    constexpr Vector4 operator-(const Vector4& value)
    {
        return Vector4(-value.x, -value.y, -value.z, -value.w);
    }
    constexpr Vector4 operator+(Vector4 left, const Vector4& right)
    {
        return left += right;
    }
    constexpr Vector4 operator-(Vector4 left, const Vector4& right)
    {
        return left -= right;
    }
    constexpr Vector4 operator*(Vector4 left, const Vector4& right)
    {
        return left *= right;
    }
    constexpr Vector4 operator/(Vector4 left, const Vector4& right)
    {
        return left /= right;
    }
    constexpr Vector4 operator*(Vector4 value, float scalar)
    {
        return value *= scalar;
    }
    constexpr Vector4 operator*(float scalar, Vector4 value)
    {
        return value *= scalar;
    }
    constexpr Vector4 operator/(Vector4 value, float scalar)
    {
        return value /= scalar;
    }
    constexpr bool operator==(const Vector4& left, const Vector4& right)
    {
        return left.x == right.x &&
            left.y == right.y &&
            left.z == right.z &&
            left.w == right.w;
    }
    constexpr bool operator!=(const Vector4& left, const Vector4& right)
    {
        return !(left == right);
    }
    constexpr float dot(const Vector4& left, const Vector4& right)
    {
        return left.x * right.x +
            left.y * right.y +
            left.z * right.z +
            left.w * right.w;
    }
    constexpr float length_squared(const Vector4& value)
    {
        return dot(value, value);
    }
    inline float length(const Vector4& value)
    {
        return sqrt(length_squared(value));
    }
    constexpr float distance_squared(const Vector4& left, const Vector4& right)
    {
        return length_squared(right - left);
    }
    inline float distance(const Vector4& left, const Vector4& right)
    {
        return sqrt(distance_squared(left, right));
    }
    inline bool is_finite(const Vector4& value)
    {
        return is_finite(value.x) &&
            is_finite(value.y) &&
            is_finite(value.z) &&
            is_finite(value.w);
    }
    inline bool is_nearly_equal(
        const Vector4& left,
        const Vector4& right,
        float tolerance = k_default_float_tolerance)
    {
        return is_nearly_equal(left.x, right.x, tolerance) &&
            is_nearly_equal(left.y, right.y, tolerance) &&
            is_nearly_equal(left.z, right.z, tolerance) &&
            is_nearly_equal(left.w, right.w, tolerance);
    }
    inline bool try_normalize(const Vector4& value, Vector4& result)
    {
        const float value_length_squared = length_squared(value);
        if (!is_finite(value) ||
            !is_finite(value_length_squared) ||
            value_length_squared <= k_normalization_tolerance_squared)
        {
            return false;
        }

        const Vector4 normalized = value * inverse_sqrt(value_length_squared);
        if (!is_finite(normalized))
        {
            return false;
        }
        result = normalized;
        return true;
    }
    inline Vector4 normalized_or_zero(const Vector4& value)
    {
        Vector4 result;
        try_normalize(value, result);
        return result;
    }
    inline Vector4 normalize_unchecked(const Vector4& value)
    {
        const float value_length_squared = length_squared(value);
        assert(is_finite(value) &&
            is_finite(value_length_squared) &&
            value_length_squared > k_normalization_tolerance_squared);
        return value * inverse_sqrt(value_length_squared);
    }
}
