#pragma once

#include "math/vector3.h"
#include "math/vector4.h"

namespace toy3d
{
    class Plane
    {
      public:
        constexpr Plane() = default;

        constexpr const Vector3& normal() const { return normal_; }

        constexpr float offset() const { return offset_; }

        constexpr float signed_distance(const Vector3& point) const { return dot(normal_, point) + offset_; }

      private:
        friend bool try_make_plane(const Vector4& coefficients, Plane& result);

        Vector3 normal_;
        float offset_ = 0.0f;
    };

    bool try_make_plane(const Vector4& coefficients, Plane& result);
} // namespace toy3d
