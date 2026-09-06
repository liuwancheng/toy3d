#include "math/quaternion.h"

#include <cassert>
#include <cmath>

namespace toy3d
{
    namespace
    {
        constexpr float k_rotation_validation_tolerance = 1.0e-5f;
        constexpr float k_slerp_linear_threshold = 0.9995f;

        float quaternion_dot(const Quaternion& left, const Quaternion& right)
        {
            return left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
        }

        Quaternion scale(const Quaternion& value, float scalar)
        {
            return Quaternion(value.x * scalar, value.y * scalar, value.z * scalar, value.w * scalar);
        }

        Quaternion add(const Quaternion& left, const Quaternion& right)
        {
            return Quaternion(left.x + right.x, left.y + right.y, left.z + right.z, left.w + right.w);
        }

        bool is_rotation_matrix(const Matrix3& matrix)
        {
            if (!is_finite(matrix))
            {
                return false;
            }
            const Vector3 column0(matrix.at(0, 0), matrix.at(0, 1), matrix.at(0, 2));
            const Vector3 column1(matrix.at(1, 0), matrix.at(1, 1), matrix.at(1, 2));
            const Vector3 column2(matrix.at(2, 0), matrix.at(2, 1), matrix.at(2, 2));
            return is_nearly_equal(length_squared(column0), 1.0f, k_rotation_validation_tolerance) &&
                   is_nearly_equal(length_squared(column1), 1.0f, k_rotation_validation_tolerance) &&
                   is_nearly_equal(length_squared(column2), 1.0f, k_rotation_validation_tolerance) &&
                   is_nearly_zero(dot(column0, column1), k_rotation_validation_tolerance) &&
                   is_nearly_zero(dot(column0, column2), k_rotation_validation_tolerance) &&
                   is_nearly_zero(dot(column1, column2), k_rotation_validation_tolerance) &&
                   is_nearly_equal(determinant(matrix), 1.0f, k_rotation_validation_tolerance);
        }
    } // namespace

    Quaternion operator*(const Quaternion& left, const Quaternion& right)
    {
        return Quaternion(left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
                          left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
                          left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
                          left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z);
    }

    bool is_finite(const Quaternion& value)
    {
        return is_finite(value.x) && is_finite(value.y) && is_finite(value.z) && is_finite(value.w);
    }

    bool is_nearly_equal(const Quaternion& left, const Quaternion& right, float tolerance)
    {
        return is_nearly_equal(left.x, right.x, tolerance) && is_nearly_equal(left.y, right.y, tolerance) &&
               is_nearly_equal(left.z, right.z, tolerance) && is_nearly_equal(left.w, right.w, tolerance);
    }

    bool is_nearly_same_rotation(const Quaternion& left, const Quaternion& right, float tolerance)
    {
        Quaternion normalized_left;
        Quaternion normalized_right;
        if (!try_normalize(left, normalized_left) || !try_normalize(right, normalized_right))
        {
            return false;
        }
        return is_nearly_equal(abs(quaternion_dot(normalized_left, normalized_right)), 1.0f, tolerance);
    }

    float length_squared(const Quaternion& value)
    {
        return quaternion_dot(value, value);
    }

    float length(const Quaternion& value)
    {
        return sqrt(length_squared(value));
    }

    bool try_normalize(const Quaternion& value, Quaternion& result)
    {
        const float value_length_squared = length_squared(value);
        if (!is_finite(value) || !is_finite(value_length_squared) ||
            value_length_squared <= k_normalization_tolerance_squared)
        {
            return false;
        }
        const Quaternion normalized = scale(value, inverse_sqrt(value_length_squared));
        if (!is_finite(normalized))
        {
            return false;
        }
        result = normalized;
        return true;
    }

    Quaternion normalized_or_identity(const Quaternion& value)
    {
        Quaternion result;
        try_normalize(value, result);
        return result;
    }

    Quaternion normalize_unchecked(const Quaternion& value)
    {
        const float value_length_squared = length_squared(value);
        assert(is_finite(value) && is_finite(value_length_squared) &&
               value_length_squared > k_normalization_tolerance_squared);
        return scale(value, inverse_sqrt(value_length_squared));
    }

    Quaternion conjugate(const Quaternion& value)
    {
        return Quaternion(-value.x, -value.y, -value.z, value.w);
    }

    bool try_inverse(const Quaternion& value, Quaternion& result)
    {
        const float value_length_squared = length_squared(value);
        if (!is_finite(value) || !is_finite(value_length_squared) ||
            value_length_squared <= k_normalization_tolerance_squared)
        {
            return false;
        }
        const Quaternion inverse = scale(conjugate(value), 1.0f / value_length_squared);
        if (!is_finite(inverse))
        {
            return false;
        }
        result = inverse;
        return true;
    }

    Quaternion inverse_unchecked(const Quaternion& value)
    {
        const float value_length_squared = length_squared(value);
        assert(is_finite(value) && is_finite(value_length_squared) &&
               value_length_squared > k_normalization_tolerance_squared);
        return scale(conjugate(value), 1.0f / value_length_squared);
    }

    bool try_make_quaternion_from_axis_angle(const Vector3& axis, Radians angle, Quaternion& result)
    {
        Vector3 normalized_axis;
        if (!is_finite(angle) || !try_normalize(axis, normalized_axis))
        {
            return false;
        }
        const float half_angle = angle.value() * 0.5f;
        const float axis_scale = std::sin(half_angle);
        const Quaternion rotation(normalized_axis.x * axis_scale, normalized_axis.y * axis_scale,
                                  normalized_axis.z * axis_scale, std::cos(half_angle));
        if (!is_finite(rotation))
        {
            return false;
        }
        result = rotation;
        return true;
    }

    bool try_make_quaternion_from_rotation_matrix(const Matrix3& matrix, Quaternion& result)
    {
        if (!is_rotation_matrix(matrix))
        {
            return false;
        }

        Quaternion rotation;
        const float trace = matrix.at(0, 0) + matrix.at(1, 1) + matrix.at(2, 2);
        if (trace > 0.0f)
        {
            const float scale_value = sqrt(trace + 1.0f) * 2.0f;
            rotation.w = 0.25f * scale_value;
            rotation.x = (matrix.at(1, 2) - matrix.at(2, 1)) / scale_value;
            rotation.y = (matrix.at(2, 0) - matrix.at(0, 2)) / scale_value;
            rotation.z = (matrix.at(0, 1) - matrix.at(1, 0)) / scale_value;
        }
        else if (matrix.at(0, 0) > matrix.at(1, 1) && matrix.at(0, 0) > matrix.at(2, 2))
        {
            const float scale_value = sqrt(1.0f + matrix.at(0, 0) - matrix.at(1, 1) - matrix.at(2, 2)) * 2.0f;
            rotation.w = (matrix.at(1, 2) - matrix.at(2, 1)) / scale_value;
            rotation.x = 0.25f * scale_value;
            rotation.y = (matrix.at(1, 0) + matrix.at(0, 1)) / scale_value;
            rotation.z = (matrix.at(2, 0) + matrix.at(0, 2)) / scale_value;
        }
        else if (matrix.at(1, 1) > matrix.at(2, 2))
        {
            const float scale_value = sqrt(1.0f + matrix.at(1, 1) - matrix.at(0, 0) - matrix.at(2, 2)) * 2.0f;
            rotation.w = (matrix.at(2, 0) - matrix.at(0, 2)) / scale_value;
            rotation.x = (matrix.at(1, 0) + matrix.at(0, 1)) / scale_value;
            rotation.y = 0.25f * scale_value;
            rotation.z = (matrix.at(2, 1) + matrix.at(1, 2)) / scale_value;
        }
        else
        {
            const float scale_value = sqrt(1.0f + matrix.at(2, 2) - matrix.at(0, 0) - matrix.at(1, 1)) * 2.0f;
            rotation.w = (matrix.at(0, 1) - matrix.at(1, 0)) / scale_value;
            rotation.x = (matrix.at(2, 0) + matrix.at(0, 2)) / scale_value;
            rotation.y = (matrix.at(2, 1) + matrix.at(1, 2)) / scale_value;
            rotation.z = 0.25f * scale_value;
        }
        return try_normalize(rotation, result);
    }

    bool try_make_quaternion_between_directions(const Vector3& from, const Vector3& to, Quaternion& result)
    {
        Vector3 normalized_from;
        Vector3 normalized_to;
        if (!try_normalize(from, normalized_from) || !try_normalize(to, normalized_to))
        {
            return false;
        }
        const float direction_dot = clamp(dot(normalized_from, normalized_to), -1.0f, 1.0f);
        if (direction_dot <= -1.0f + k_rotation_validation_tolerance)
        {
            return false;
        }
        if (direction_dot >= 1.0f - k_rotation_validation_tolerance)
        {
            result = Quaternion::identity();
            return true;
        }
        const Vector3 rotation_axis = cross(normalized_from, normalized_to);
        const Quaternion candidate(rotation_axis.x, rotation_axis.y, rotation_axis.z, 1.0f + direction_dot);
        return try_normalize(candidate, result);
    }

    bool try_make_rotation_from_forward_up(const Vector3& forward, const Vector3& requested_up, Quaternion& result)
    {
        Vector3 normalized_forward;
        Vector3 normalized_up;
        if (!try_normalize(forward, normalized_forward) || !try_normalize(requested_up, normalized_up))
        {
            return false;
        }

        Vector3 right;
        if (!try_normalize(cross(normalized_up, normalized_forward), right))
        {
            return false;
        }
        const Vector3 up = cross(normalized_forward, right);
        Quaternion rotation;
        const Matrix3 rotation_matrix(right, up, normalized_forward);
        if (!try_make_quaternion_from_rotation_matrix(rotation_matrix, rotation))
        {
            return false;
        }
        result = rotation;
        return true;
    }

    Vector3 rotate_vector(const Quaternion& rotation, const Vector3& vector)
    {
        const Quaternion normalized = normalize_unchecked(rotation);
        const Vector3 quaternion_vector(normalized.x, normalized.y, normalized.z);
        const Vector3 doubled_cross = 2.0f * cross(quaternion_vector, vector);
        return vector + normalized.w * doubled_cross + cross(quaternion_vector, doubled_cross);
    }

    Vector3 unrotate_vector(const Quaternion& rotation, const Vector3& vector)
    {
        return rotate_vector(conjugate(normalize_unchecked(rotation)), vector);
    }

    Matrix3 to_matrix3(const Quaternion& rotation)
    {
        const Quaternion value = normalize_unchecked(rotation);
        const float xx = value.x * value.x;
        const float yy = value.y * value.y;
        const float zz = value.z * value.z;
        const float xy = value.x * value.y;
        const float xz = value.x * value.z;
        const float yz = value.y * value.z;
        const float xw = value.x * value.w;
        const float yw = value.y * value.w;
        const float zw = value.z * value.w;
        return Matrix3(Vector3(1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw), 2.0f * (xz - yw)),
                       Vector3(2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + xw)),
                       Vector3(2.0f * (xz + yw), 2.0f * (yz - xw), 1.0f - 2.0f * (xx + yy)));
    }

    Matrix4 to_matrix4(const Quaternion& rotation)
    {
        const Matrix3 matrix = to_matrix3(rotation);
        Matrix4 result;
        for (std::size_t column = 0; column < Matrix3::k_column_count; ++column)
        {
            for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
            {
                result.at(column, row) = matrix.at(column, row);
            }
        }
        return result;
    }

    bool try_slerp(const Quaternion& start, const Quaternion& end, float alpha, Quaternion& result)
    {
        Quaternion normalized_start;
        Quaternion normalized_end;
        if (!is_finite(alpha) || !try_normalize(start, normalized_start) || !try_normalize(end, normalized_end))
        {
            return false;
        }

        float cosine = quaternion_dot(normalized_start, normalized_end);
        if (cosine < 0.0f)
        {
            normalized_end = -normalized_end;
            cosine = -cosine;
        }
        cosine = clamp(cosine, -1.0f, 1.0f);

        Quaternion interpolated;
        if (cosine >= k_slerp_linear_threshold)
        {
            interpolated = add(scale(normalized_start, 1.0f - alpha), scale(normalized_end, alpha));
        }
        else
        {
            const float angle = std::acos(cosine);
            const float inverse_sine = 1.0f / std::sin(angle);
            interpolated = add(scale(normalized_start, std::sin((1.0f - alpha) * angle) * inverse_sine),
                               scale(normalized_end, std::sin(alpha * angle) * inverse_sine));
        }
        return try_normalize(interpolated, result);
    }
} // namespace toy3d
