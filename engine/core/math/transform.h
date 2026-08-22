#pragma once

#include "math/quaternion.h"

#include <type_traits>

namespace toy3d
{
    struct Transform
    {
        Vector3 translation;
        Quaternion rotation;
        Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    static_assert(sizeof(Transform) == sizeof(float) * 10,
        "Transform must contain exactly ten contiguous scalar values.");
    static_assert(std::is_standard_layout<Transform>::value,
        "Transform must be standard-layout.");
    static_assert(std::is_trivially_copyable<Transform>::value,
        "Transform must be trivially copyable.");

    Matrix4 to_matrix(const Transform& transform);
    bool try_decompose_transform(const Matrix4& matrix, Transform& result);

    Vector3 transform_position(
        const Transform& transform,
        const Vector3& position);
    Vector3 transform_vector(
        const Transform& transform,
        const Vector3& vector);
    Vector3 transform_direction(
        const Transform& transform,
        const Vector3& direction);
    Vector3 inverse_transform_position(
        const Transform& transform,
        const Vector3& position);
    Vector3 inverse_transform_vector(
        const Transform& transform,
        const Vector3& vector);

    Vector3 right(const Transform& transform);
    Vector3 up(const Transform& transform);
    Vector3 forward(const Transform& transform);
}
