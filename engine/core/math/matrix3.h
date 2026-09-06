#pragma once

#include "math/vector3.h"

#include <cstddef>
#include <type_traits>

namespace toy3d
{
    class Matrix3
    {
      public:
        // C++17 makes constexpr static data members inline, so dimensions can
        // safely drive storage and loops without a separate definition.
        static constexpr std::size_t k_column_count = 3;
        static constexpr std::size_t k_row_count = 3;
        static constexpr std::size_t k_element_count = k_column_count * k_row_count;

        constexpr Matrix3() : values_{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f} {}

        explicit constexpr Matrix3(float diagonal)
            : values_{diagonal, 0.0f, 0.0f, 0.0f, diagonal, 0.0f, 0.0f, 0.0f, diagonal}
        {
        }

        constexpr Matrix3(const Vector3& column0, const Vector3& column1, const Vector3& column2)
            : values_{column0.x, column0.y, column0.z, column1.x, column1.y, column1.z, column2.x, column2.y, column2.z}
        {
        }

        float& at(std::size_t column, std::size_t row);
        const float& at(std::size_t column, std::size_t row) const;

        constexpr float* data() { return values_; }

        constexpr const float* data() const { return values_; }

        static constexpr Matrix3 identity() { return Matrix3(); }

        static constexpr Matrix3 zero() { return Matrix3(0.0f); }

      private:
        float values_[k_element_count];
    };

    static_assert(sizeof(Matrix3) == sizeof(float) * Matrix3::k_element_count,
                  "Matrix3 must contain exactly nine contiguous floats.");
    static_assert(alignof(Matrix3) == alignof(float), "Matrix3 must retain scalar alignment.");
    static_assert(std::is_standard_layout<Matrix3>::value, "Matrix3 must be standard-layout.");
    static_assert(std::is_trivially_copyable<Matrix3>::value, "Matrix3 must be trivially copyable.");

    bool operator==(const Matrix3& left, const Matrix3& right);
    bool operator!=(const Matrix3& left, const Matrix3& right);
    Matrix3 operator*(const Matrix3& left, const Matrix3& right);
    Vector3 operator*(const Matrix3& matrix, const Vector3& vector);
    bool is_finite(const Matrix3& value);
    bool is_nearly_equal(const Matrix3& left, const Matrix3& right, float tolerance = k_default_float_tolerance);
    Matrix3 transpose(const Matrix3& value);
    float determinant(const Matrix3& value);
    bool try_inverse(const Matrix3& value, Matrix3& result);
} // namespace toy3d
