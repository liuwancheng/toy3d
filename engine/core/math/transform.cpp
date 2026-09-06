#include "math/transform.h"

namespace toy3d
{
    namespace
    {
        constexpr float k_transform_decomposition_tolerance = 1.0e-5f;

        bool has_affine_last_row(const Matrix4& matrix)
        {
            return is_nearly_zero(matrix.at(0, 3), k_transform_decomposition_tolerance) &&
                   is_nearly_zero(matrix.at(1, 3), k_transform_decomposition_tolerance) &&
                   is_nearly_zero(matrix.at(2, 3), k_transform_decomposition_tolerance) &&
                   is_nearly_equal(matrix.at(3, 3), 1.0f, k_transform_decomposition_tolerance);
        }
    } // namespace

    Matrix4 to_matrix(const Transform& transform)
    {
        Matrix4 result = to_matrix4(transform.rotation);
        for (std::size_t row = 0; row < 3; ++row)
        {
            result.at(0, row) *= transform.scale.x;
            result.at(1, row) *= transform.scale.y;
            result.at(2, row) *= transform.scale.z;
        }
        result.at(3, 0) = transform.translation.x;
        result.at(3, 1) = transform.translation.y;
        result.at(3, 2) = transform.translation.z;
        return result;
    }

    bool try_decompose_transform(const Matrix4& matrix, Transform& result)
    {
        if (!is_finite(matrix) || !has_affine_last_row(matrix))
        {
            return false;
        }

        const Vector3 column0(matrix.at(0, 0), matrix.at(0, 1), matrix.at(0, 2));
        const Vector3 column1(matrix.at(1, 0), matrix.at(1, 1), matrix.at(1, 2));
        const Vector3 column2(matrix.at(2, 0), matrix.at(2, 1), matrix.at(2, 2));
        const Vector3 scale_value(length(column0), length(column1), length(column2));
        if (!is_finite(scale_value) || scale_value.x <= k_default_float_tolerance ||
            scale_value.y <= k_default_float_tolerance || scale_value.z <= k_default_float_tolerance)
        {
            return false;
        }

        const Vector3 right_axis = column0 / scale_value.x;
        const Vector3 up_axis = column1 / scale_value.y;
        const Vector3 forward_axis = column2 / scale_value.z;
        if (!is_nearly_zero(dot(right_axis, up_axis), k_transform_decomposition_tolerance) ||
            !is_nearly_zero(dot(right_axis, forward_axis), k_transform_decomposition_tolerance) ||
            !is_nearly_zero(dot(up_axis, forward_axis), k_transform_decomposition_tolerance))
        {
            return false;
        }

        Quaternion rotation_value;
        const Matrix3 rotation_matrix(right_axis, up_axis, forward_axis);
        if (!try_make_quaternion_from_rotation_matrix(rotation_matrix, rotation_value))
        {
            return false;
        }

        Transform decomposed;
        decomposed.translation = Vector3(matrix.at(3, 0), matrix.at(3, 1), matrix.at(3, 2));
        decomposed.rotation = rotation_value;
        decomposed.scale = scale_value;
        if (!is_nearly_equal(to_matrix(decomposed), matrix, k_transform_decomposition_tolerance))
        {
            return false;
        }
        result = decomposed;
        return true;
    }

    Vector3 transform_position(const Transform& transform, const Vector3& position)
    {
        return transform.translation + rotate_vector(transform.rotation, transform.scale * position);
    }

    Vector3 transform_vector(const Transform& transform, const Vector3& vector)
    {
        return rotate_vector(transform.rotation, transform.scale * vector);
    }

    Vector3 transform_direction(const Transform& transform, const Vector3& direction)
    {
        return rotate_vector(transform.rotation, direction);
    }

    Vector3 inverse_transform_position(const Transform& transform, const Vector3& position)
    {
        return unrotate_vector(transform.rotation, position - transform.translation) / transform.scale;
    }

    Vector3 inverse_transform_vector(const Transform& transform, const Vector3& vector)
    {
        return unrotate_vector(transform.rotation, vector) / transform.scale;
    }

    Vector3 right(const Transform& transform)
    {
        return transform_direction(transform, Vector3(1.0f, 0.0f, 0.0f));
    }

    Vector3 up(const Transform& transform)
    {
        return transform_direction(transform, Vector3(0.0f, 1.0f, 0.0f));
    }

    Vector3 forward(const Transform& transform)
    {
        return transform_direction(transform, Vector3(0.0f, 0.0f, 1.0f));
    }
} // namespace toy3d
