#include "math/matrix4.h"

#include <cassert>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        Matrix3 linear_matrix(const Matrix4& value)
        {
            return Matrix3(Vector3(value.at(0, 0), value.at(0, 1), value.at(0, 2)),
                           Vector3(value.at(1, 0), value.at(1, 1), value.at(1, 2)),
                           Vector3(value.at(2, 0), value.at(2, 1), value.at(2, 2)));
        }
    } // namespace

    float& Matrix4::at(std::size_t column, std::size_t row)
    {
        assert(column < k_column_count && row < k_row_count);
        return values_[column * k_row_count + row];
    }

    const float& Matrix4::at(std::size_t column, std::size_t row) const
    {
        assert(column < k_column_count && row < k_row_count);
        return values_[column * k_row_count + row];
    }

    bool operator==(const Matrix4& left, const Matrix4& right)
    {
        for (std::size_t index = 0; index < Matrix4::k_element_count; ++index)
        {
            if (left.data()[index] != right.data()[index])
            {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const Matrix4& left, const Matrix4& right)
    {
        return !(left == right);
    }

    Matrix4 operator*(const Matrix4& left, const Matrix4& right)
    {
        Matrix4 result = Matrix4::zero();
        for (std::size_t column = 0; column < Matrix4::k_column_count; ++column)
        {
            for (std::size_t row = 0; row < Matrix4::k_row_count; ++row)
            {
                for (std::size_t index = 0; index < Matrix4::k_column_count; ++index)
                {
                    result.at(column, row) += left.at(index, row) * right.at(column, index);
                }
            }
        }
        return result;
    }

    Vector4 operator*(const Matrix4& matrix, const Vector4& vector)
    {
        return Vector4(matrix.at(0, 0) * vector.x + matrix.at(1, 0) * vector.y + matrix.at(2, 0) * vector.z +
                           matrix.at(3, 0) * vector.w,
                       matrix.at(0, 1) * vector.x + matrix.at(1, 1) * vector.y + matrix.at(2, 1) * vector.z +
                           matrix.at(3, 1) * vector.w,
                       matrix.at(0, 2) * vector.x + matrix.at(1, 2) * vector.y + matrix.at(2, 2) * vector.z +
                           matrix.at(3, 2) * vector.w,
                       matrix.at(0, 3) * vector.x + matrix.at(1, 3) * vector.y + matrix.at(2, 3) * vector.z +
                           matrix.at(3, 3) * vector.w);
    }

    bool is_finite(const Matrix4& value)
    {
        for (std::size_t index = 0; index < Matrix4::k_element_count; ++index)
        {
            if (!is_finite(value.data()[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool is_nearly_equal(const Matrix4& left, const Matrix4& right, float tolerance)
    {
        for (std::size_t index = 0; index < Matrix4::k_element_count; ++index)
        {
            if (!is_nearly_equal(left.data()[index], right.data()[index], tolerance))
            {
                return false;
            }
        }
        return true;
    }

    Matrix4 transpose(const Matrix4& value)
    {
        Matrix4 result = Matrix4::zero();
        for (std::size_t column = 0; column < Matrix4::k_column_count; ++column)
        {
            for (std::size_t row = 0; row < Matrix4::k_row_count; ++row)
            {
                result.at(column, row) = value.at(row, column);
            }
        }
        return result;
    }

    float determinant(const Matrix4& value)
    {
        if (!is_finite(value))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }

        float rows[Matrix4::k_row_count][Matrix4::k_column_count] = {};
        for (std::size_t row = 0; row < Matrix4::k_row_count; ++row)
        {
            for (std::size_t column = 0; column < Matrix4::k_column_count; ++column)
            {
                rows[row][column] = value.at(column, row);
            }
        }

        float result = 1.0f;
        for (std::size_t pivot_column = 0; pivot_column < Matrix4::k_column_count; ++pivot_column)
        {
            std::size_t pivot_row = pivot_column;
            float pivot_magnitude = abs(rows[pivot_row][pivot_column]);
            for (std::size_t row = pivot_column + 1; row < Matrix4::k_row_count; ++row)
            {
                const float candidate = abs(rows[row][pivot_column]);
                if (candidate > pivot_magnitude)
                {
                    pivot_row = row;
                    pivot_magnitude = candidate;
                }
            }
            if (pivot_magnitude == 0.0f)
            {
                return 0.0f;
            }
            if (pivot_row != pivot_column)
            {
                for (std::size_t column = 0; column < Matrix4::k_column_count; ++column)
                {
                    std::swap(rows[pivot_column][column], rows[pivot_row][column]);
                }
                result = -result;
            }

            const float pivot = rows[pivot_column][pivot_column];
            result *= pivot;
            for (std::size_t row = pivot_column + 1; row < Matrix4::k_row_count; ++row)
            {
                const float factor = rows[row][pivot_column] / pivot;
                for (std::size_t column = pivot_column + 1; column < Matrix4::k_column_count; ++column)
                {
                    rows[row][column] -= factor * rows[pivot_column][column];
                }
            }
        }
        return result;
    }

    bool try_inverse(const Matrix4& value, Matrix4& result)
    {
        constexpr std::size_t k_augmented_column_count = Matrix4::k_column_count * 2;
        float augmented[Matrix4::k_row_count][k_augmented_column_count] = {};
        for (std::size_t row = 0; row < Matrix4::k_row_count; ++row)
        {
            for (std::size_t column = 0; column < Matrix4::k_column_count; ++column)
            {
                augmented[row][column] = value.at(column, row);
                augmented[row][Matrix4::k_column_count + column] = row == column ? 1.0f : 0.0f;
            }
        }
        if (!is_finite(value))
        {
            return false;
        }

        for (std::size_t pivot_column = 0; pivot_column < Matrix4::k_column_count; ++pivot_column)
        {
            std::size_t pivot_row = pivot_column;
            float pivot_magnitude = abs(augmented[pivot_row][pivot_column]);
            for (std::size_t row = pivot_column + 1; row < Matrix4::k_row_count; ++row)
            {
                const float candidate = abs(augmented[row][pivot_column]);
                if (candidate > pivot_magnitude)
                {
                    pivot_row = row;
                    pivot_magnitude = candidate;
                }
            }
            if (!is_finite(pivot_magnitude) || pivot_magnitude <= k_matrix_inverse_tolerance)
            {
                return false;
            }

            if (pivot_row != pivot_column)
            {
                for (std::size_t column = 0; column < k_augmented_column_count; ++column)
                {
                    std::swap(augmented[pivot_column][column], augmented[pivot_row][column]);
                }
            }

            const float pivot = augmented[pivot_column][pivot_column];
            for (std::size_t column = 0; column < k_augmented_column_count; ++column)
            {
                augmented[pivot_column][column] /= pivot;
            }
            for (std::size_t row = 0; row < Matrix4::k_row_count; ++row)
            {
                if (row == pivot_column)
                {
                    continue;
                }
                const float factor = augmented[row][pivot_column];
                for (std::size_t column = 0; column < k_augmented_column_count; ++column)
                {
                    augmented[row][column] -= factor * augmented[pivot_column][column];
                }
            }
        }

        Matrix4 inverse = Matrix4::zero();
        for (std::size_t row = 0; row < Matrix4::k_row_count; ++row)
        {
            for (std::size_t column = 0; column < Matrix4::k_column_count; ++column)
            {
                inverse.at(column, row) = augmented[row][Matrix4::k_column_count + column];
            }
        }
        if (!is_finite(inverse))
        {
            return false;
        }
        result = inverse;
        return true;
    }

    Vector3 transform_position(const Matrix4& matrix, const Vector3& position)
    {
        const Vector4 transformed = matrix * Vector4(position, 1.0f);
        return Vector3(transformed.x, transformed.y, transformed.z);
    }

    Vector3 transform_vector(const Matrix4& matrix, const Vector3& vector)
    {
        const Vector4 transformed = matrix * Vector4(vector, 0.0f);
        return Vector3(transformed.x, transformed.y, transformed.z);
    }

    bool try_transform_normal(const Matrix4& matrix, const Vector3& normal, Vector3& result)
    {
        Matrix3 inverse;
        if (!try_inverse(linear_matrix(matrix), inverse))
        {
            return false;
        }
        const Vector3 transformed = transpose(inverse) * normal;
        Vector3 normalized;
        if (!try_normalize(transformed, normalized))
        {
            return false;
        }
        result = normalized;
        return true;
    }
} // namespace toy3d
