#pragma once

#include "math/angle.h"
#include "math/matrix4.h"

#include <type_traits>

namespace toy3d
{
    // Quaternion scalar storage order is x, y, z, w. The vector part comes
    // first so data() and serialization boundaries have one stable contract.
    struct Quaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        constexpr Quaternion() = default;
        constexpr Quaternion(
            float x_value,
            float y_value,
            float z_value,
            float w_value)
            : x(x_value), y(y_value), z(z_value), w(w_value)
        {}

        constexpr float* data()
        {
            return &x;
        }

        constexpr const float* data() const
        {
            return &x;
        }

        static constexpr Quaternion identity()
        {
            return Quaternion();
        }
    };

    static_assert(sizeof(Quaternion) == sizeof(float) * 4,
        "Quaternion must contain exactly four contiguous floats.");
    static_assert(alignof(Quaternion) == alignof(float),
        "Quaternion must retain scalar alignment.");
    static_assert(std::is_standard_layout<Quaternion>::value,
        "Quaternion must be standard-layout.");
    static_assert(std::is_trivially_copyable<Quaternion>::value,
        "Quaternion must be trivially copyable.");

    constexpr Quaternion operator-(const Quaternion& value)
    {
        return Quaternion(-value.x, -value.y, -value.z, -value.w);
    }

    constexpr bool operator==(const Quaternion& left, const Quaternion& right)
    {
        return left.x == right.x &&
            left.y == right.y &&
            left.z == right.z &&
            left.w == right.w;
    }

    constexpr bool operator!=(const Quaternion& left, const Quaternion& right)
    {
        return !(left == right);
    }

    Quaternion operator*(const Quaternion& left, const Quaternion& right);
    bool is_finite(const Quaternion& value);
    bool is_nearly_equal(
        const Quaternion& left,
        const Quaternion& right,
        float tolerance = k_default_float_tolerance);
    bool is_nearly_same_rotation(
        const Quaternion& left,
        const Quaternion& right,
        float tolerance = k_default_float_tolerance);
    float length_squared(const Quaternion& value);
    float length(const Quaternion& value);
    bool try_normalize(const Quaternion& value, Quaternion& result);
    Quaternion normalized_or_identity(const Quaternion& value);
    Quaternion normalize_unchecked(const Quaternion& value);
    Quaternion conjugate(const Quaternion& value);
    bool try_inverse(const Quaternion& value, Quaternion& result);
    Quaternion inverse_unchecked(const Quaternion& value);

    bool try_make_quaternion_from_axis_angle(
        const Vector3& axis,
        Radians angle,
        Quaternion& result);
    bool try_make_quaternion_from_rotation_matrix(
        const Matrix3& matrix,
        Quaternion& result);
    bool try_make_quaternion_between_directions(
        const Vector3& from,
        const Vector3& to,
        Quaternion& result);
    bool try_make_rotation_from_forward_up(
        const Vector3& forward,
        const Vector3& requested_up,
        Quaternion& result);

    // These direct operations require a finite, non-zero rotation. They
    // normalize its magnitude and assert the precondition in Debug builds.
    Vector3 rotate_vector(const Quaternion& rotation, const Vector3& vector);
    Vector3 unrotate_vector(const Quaternion& rotation, const Vector3& vector);
    Matrix3 to_matrix3(const Quaternion& rotation);
    Matrix4 to_matrix4(const Quaternion& rotation);
    bool try_slerp(
        const Quaternion& start,
        const Quaternion& end,
        float alpha,
        Quaternion& result);
}
