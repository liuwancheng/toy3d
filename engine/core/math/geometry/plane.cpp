#include "math/geometry/plane.h"

namespace toy3d
{
    bool try_make_plane(const Vector4& coefficients, Plane& result)
    {
        const Vector3 normal(coefficients.x, coefficients.y, coefficients.z);
        const float normal_length_squared = length_squared(normal);
        if (!is_finite(coefficients) || !is_finite(normal_length_squared) ||
            normal_length_squared <= k_normalization_tolerance_squared)
        {
            return false;
        }

        const float inverse_normal_length = inverse_sqrt(normal_length_squared);
        Plane plane;
        plane.normal_ = normal * inverse_normal_length;
        plane.offset_ = coefficients.w * inverse_normal_length;
        if (!is_finite(plane.normal_) || !is_finite(plane.offset_))
        {
            return false;
        }

        result = plane;
        return true;
    }
} // namespace toy3d
