#pragma once

#include "math/angle.h"
#include "math/quaternion.h"

namespace toy3d
{
    // World-to-view construction contains no Camera, viewport, fallback-up,
    // or graphics-backend policy.
    bool try_make_view_matrix(const Vector3& position, const Quaternion& orientation, Matrix4& result);
    bool try_make_look_at_view_matrix(const Vector3& eye, const Vector3& target, const Vector3& requested_up,
                                      Matrix4& result);

    // Toy3d has one public projection convention: left-handed, 0..1
    // reversed-Z. Backend-specific viewport Y correction does not belong here.
    struct PerspectiveProjectionDesc
    {
        Radians vertical_fov{Radians(k_pi / 3.0f)};
        float aspect = 1.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };

    struct InfinitePerspectiveProjectionDesc
    {
        Radians vertical_fov{Radians(k_pi / 3.0f)};
        float aspect = 1.0f;
        float near_clip = 0.1f;
    };

    struct OrthographicProjectionDesc
    {
        float left = -1.0f;
        float right = 1.0f;
        float bottom = -1.0f;
        float top = 1.0f;
        float near_clip = 0.1f;
        float far_clip = 100.0f;
    };

    bool try_make_perspective_projection(const PerspectiveProjectionDesc& desc, Matrix4& result);
    bool try_make_infinite_perspective_projection(const InfinitePerspectiveProjectionDesc& desc, Matrix4& result);
    bool try_make_orthographic_projection(const OrthographicProjectionDesc& desc, Matrix4& result);
} // namespace toy3d
