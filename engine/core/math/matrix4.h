#pragma once

#include "math/matrix3.h"
#include "math/vector4.h"

#include <cstddef>
#include <type_traits>

namespace toy3d
{
    class Matrix4
    {
      public:
        // C++17 makes constexpr static data members inline, so dimensions can
        // safely drive storage and loops without a separate definition.
        static constexpr std::size_t k_column_count = 4;
        static constexpr std::size_t k_row_count = 4;
        static constexpr std::size_t k_element_count = k_column_count * k_row_count;

        constexpr Matrix4()
            : values_{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}
        {
        }

        explicit constexpr Matrix4(float diagonal)
            : values_{diagonal, 0.0f, 0.0f,     0.0f, 0.0f, diagonal, 0.0f, 0.0f,
                      0.0f,     0.0f, diagonal, 0.0f, 0.0f, 0.0f,     0.0f, diagonal}
        {
        }

        constexpr Matrix4(const Vector4& column0, const Vector4& column1, const Vector4& column2,
                          const Vector4& column3)
            : values_{column0.x, column0.y, column0.z, column0.w, column1.x, column1.y, column1.z, column1.w,
                      column2.x, column2.y, column2.z, column2.w, column3.x, column3.y, column3.z, column3.w}
        {
        }

        float& at(std::size_t column, std::size_t row);
        const float& at(std::size_t column, std::size_t row) const;

        constexpr float* data()
        {
            return values_;
        }

        constexpr const float* data() const
        {
            return values_;
        }

        static constexpr Matrix4 identity()
        {
            return Matrix4();
        }

        static constexpr Matrix4 zero()
        {
            return Matrix4(0.0f);
        }

      private:
        float values_[k_element_count];
    };

    static_assert(sizeof(Matrix4) == sizeof(float) * Matrix4::k_element_count,
                  "Matrix4 must contain exactly sixteen contiguous floats.");
    static_assert(alignof(Matrix4) == alignof(float), "Matrix4 must retain scalar alignment.");
    static_assert(std::is_standard_layout<Matrix4>::value, "Matrix4 must be standard-layout.");
    static_assert(std::is_trivially_copyable<Matrix4>::value, "Matrix4 must be trivially copyable.");

    bool operator==(const Matrix4& left, const Matrix4& right);
    bool operator!=(const Matrix4& left, const Matrix4& right);
    Matrix4 operator*(const Matrix4& left, const Matrix4& right);
    Vector4 operator*(const Matrix4& matrix, const Vector4& vector);
    bool is_finite(const Matrix4& value);
    bool is_nearly_equal(const Matrix4& left, const Matrix4& right, float tolerance = k_default_float_tolerance);
    Matrix4 transpose(const Matrix4& value);
    float determinant(const Matrix4& value);
    bool try_inverse(const Matrix4& value, Matrix4& result);

    Vector3 transform_position(const Matrix4& matrix, const Vector3& position);
    Vector3 transform_vector(const Matrix4& matrix, const Vector3& vector);
    bool try_transform_normal(const Matrix4& matrix, const Vector3& normal, Vector3& result);
} // namespace toy3d
