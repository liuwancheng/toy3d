#include "math/matrix3.h"

#include <cassert>
#include <utility>

namespace toy3d
{
    float& Matrix3::at(std::size_t column, std::size_t row)
    {
        assert(column < k_column_count && row < k_row_count);
        return values_[column * k_row_count + row];
    }

    const float& Matrix3::at(std::size_t column, std::size_t row) const
    {
        assert(column < k_column_count && row < k_row_count);
        return values_[column * k_row_count + row];
    }

    bool operator==(const Matrix3& left, const Matrix3& right)
    {
        for (std::size_t index = 0; index < Matrix3::k_element_count; ++index)
        {
            if (left.data()[index] != right.data()[index])
            {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const Matrix3& left, const Matrix3& right)
    {
        return !(left == right);
    }

    Matrix3 operator*(const Matrix3& left, const Matrix3& right)
    {
        Matrix3 result = Matrix3::zero();
        for (std::size_t column = 0; column < Matrix3::k_column_count; ++column)
        {
            for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
            {
                for (std::size_t index = 0;
                    index < Matrix3::k_column_count;
                    ++index)
                {
                    result.at(column, row) +=
                        left.at(index, row) * right.at(column, index);
                }
            }
        }
        return result;
    }

    Vector3 operator*(const Matrix3& matrix, const Vector3& vector)
    {
        return Vector3(
            matrix.at(0, 0) * vector.x +
                matrix.at(1, 0) * vector.y +
                matrix.at(2, 0) * vector.z,
            matrix.at(0, 1) * vector.x +
                matrix.at(1, 1) * vector.y +
                matrix.at(2, 1) * vector.z,
            matrix.at(0, 2) * vector.x +
                matrix.at(1, 2) * vector.y +
                matrix.at(2, 2) * vector.z);
    }

    bool is_finite(const Matrix3& value)
    {
        for (std::size_t index = 0; index < Matrix3::k_element_count; ++index)
        {
            if (!is_finite(value.data()[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool is_nearly_equal(
        const Matrix3& left,
        const Matrix3& right,
        float tolerance)
    {
        for (std::size_t index = 0; index < Matrix3::k_element_count; ++index)
        {
            if (!is_nearly_equal(
                    left.data()[index], right.data()[index], tolerance))
            {
                return false;
            }
        }
        return true;
    }

    Matrix3 transpose(const Matrix3& value)
    {
        Matrix3 result = Matrix3::zero();
        for (std::size_t column = 0; column < Matrix3::k_column_count; ++column)
        {
            for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
            {
                result.at(column, row) = value.at(row, column);
            }
        }
        return result;
    }

    float determinant(const Matrix3& value)
    {
        return
            value.at(0, 0) *
                (value.at(1, 1) * value.at(2, 2) -
                    value.at(2, 1) * value.at(1, 2)) -
            value.at(1, 0) *
                (value.at(0, 1) * value.at(2, 2) -
                    value.at(2, 1) * value.at(0, 2)) +
            value.at(2, 0) *
                (value.at(0, 1) * value.at(1, 2) -
                    value.at(1, 1) * value.at(0, 2));
    }

    bool try_inverse(const Matrix3& value, Matrix3& result)
    {
        constexpr std::size_t k_augmented_column_count =
            Matrix3::k_column_count * 2;
        float augmented[Matrix3::k_row_count][k_augmented_column_count] = {};
        for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
        {
            for (std::size_t column = 0;
                column < Matrix3::k_column_count;
                ++column)
            {
                augmented[row][column] = value.at(column, row);
                augmented[row][Matrix3::k_column_count + column] =
                    row == column ? 1.0f : 0.0f;
            }
        }

        if (!is_finite(value))
        {
            return false;
        }

        for (std::size_t pivot_column = 0;
            pivot_column < Matrix3::k_column_count;
            ++pivot_column)
        {
            std::size_t pivot_row = pivot_column;
            float pivot_magnitude = abs(augmented[pivot_row][pivot_column]);
            for (std::size_t row = pivot_column + 1;
                row < Matrix3::k_row_count;
                ++row)
            {
                const float candidate = abs(augmented[row][pivot_column]);
                if (candidate > pivot_magnitude)
                {
                    pivot_row = row;
                    pivot_magnitude = candidate;
                }
            }
            if (!is_finite(pivot_magnitude) ||
                pivot_magnitude <= k_matrix_inverse_tolerance)
            {
                return false;
            }

            if (pivot_row != pivot_column)
            {
                for (std::size_t column = 0;
                    column < k_augmented_column_count;
                    ++column)
                {
                    std::swap(
                        augmented[pivot_column][column],
                        augmented[pivot_row][column]);
                }
            }

            const float pivot = augmented[pivot_column][pivot_column];
            for (std::size_t column = 0;
                column < k_augmented_column_count;
                ++column)
            {
                augmented[pivot_column][column] /= pivot;
            }
            for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
            {
                if (row == pivot_column)
                {
                    continue;
                }
                const float factor = augmented[row][pivot_column];
                for (std::size_t column = 0;
                    column < k_augmented_column_count;
                    ++column)
                {
                    augmented[row][column] -=
                        factor * augmented[pivot_column][column];
                }
            }
        }

        Matrix3 inverse = Matrix3::zero();
        for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
        {
            for (std::size_t column = 0;
                column < Matrix3::k_column_count;
                ++column)
            {
                inverse.at(column, row) =
                    augmented[row][Matrix3::k_column_count + column];
            }
        }
        if (!is_finite(inverse))
        {
            return false;
        }
        result = inverse;
        return true;
    }
}
