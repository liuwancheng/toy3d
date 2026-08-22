#include "math/matrix_construction.h"

namespace toy3d
{
    namespace
    {
        Matrix4 make_view_matrix(
            const Vector3& position,
            const Vector3& right,
            const Vector3& up,
            const Vector3& forward)
        {
            Matrix4 result;
            result.at(0, 0) = right.x;
            result.at(0, 1) = up.x;
            result.at(0, 2) = forward.x;
            result.at(1, 0) = right.y;
            result.at(1, 1) = up.y;
            result.at(1, 2) = forward.y;
            result.at(2, 0) = right.z;
            result.at(2, 1) = up.z;
            result.at(2, 2) = forward.z;
            result.at(3, 0) = -dot(right, position);
            result.at(3, 1) = -dot(up, position);
            result.at(3, 2) = -dot(forward, position);
            return result;
        }

        bool is_valid_common_projection(
            Radians vertical_fov,
            float aspect,
            float near_clip)
        {
            return is_finite(vertical_fov) &&
                is_finite(aspect) &&
                is_finite(near_clip) &&
                vertical_fov.value() > 0.0f &&
                vertical_fov.value() < k_pi &&
                aspect > 0.0f &&
                near_clip > 0.0f;
        }

        Matrix4 make_projection_common(
            Radians vertical_fov,
            float aspect)
        {
            const float inverse_tan_half_fov =
                1.0f / tan(vertical_fov * 0.5f);
            Matrix4 result = Matrix4::zero();
            result.at(0, 0) = inverse_tan_half_fov / aspect;
            result.at(1, 1) = inverse_tan_half_fov;
            result.at(2, 3) = 1.0f;
            return result;
        }
    }

    bool try_make_view_matrix(
        const Vector3& position,
        const Quaternion& orientation,
        Matrix4& result)
    {
        Quaternion normalized_orientation;
        if (!is_finite(position) ||
            !try_normalize(orientation, normalized_orientation))
        {
            return false;
        }

        const Matrix3 rotation = to_matrix3(normalized_orientation);
        const Matrix4 view = make_view_matrix(
            position,
            rotation * Vector3(1.0f, 0.0f, 0.0f),
            rotation * Vector3(0.0f, 1.0f, 0.0f),
            rotation * Vector3(0.0f, 0.0f, 1.0f));
        if (!is_finite(view))
        {
            return false;
        }
        result = view;
        return true;
    }

    bool try_make_look_at_view_matrix(
        const Vector3& eye,
        const Vector3& target,
        const Vector3& requested_up,
        Matrix4& result)
    {
        if (!is_finite(eye) || !is_finite(target))
        {
            return false;
        }

        Quaternion orientation;
        if (!try_make_rotation_from_forward_up(
                target - eye, requested_up, orientation))
        {
            return false;
        }
        return try_make_view_matrix(eye, orientation, result);
    }

    bool try_make_perspective_projection(
        const PerspectiveProjectionDesc& desc,
        Matrix4& result)
    {
        if (!is_valid_common_projection(
                desc.vertical_fov, desc.aspect, desc.near_clip) ||
            !is_finite(desc.far_clip) ||
            desc.far_clip <= desc.near_clip)
        {
            return false;
        }

        Matrix4 projection = make_projection_common(
            desc.vertical_fov, desc.aspect);
        projection.at(2, 2) =
            desc.near_clip / (desc.near_clip - desc.far_clip);
        projection.at(3, 2) =
            (desc.near_clip * desc.far_clip) /
            (desc.far_clip - desc.near_clip);
        if (!is_finite(projection))
        {
            return false;
        }
        result = projection;
        return true;
    }

    bool try_make_infinite_perspective_projection(
        const InfinitePerspectiveProjectionDesc& desc,
        Matrix4& result)
    {
        if (!is_valid_common_projection(
                desc.vertical_fov, desc.aspect, desc.near_clip))
        {
            return false;
        }

        Matrix4 projection = make_projection_common(
            desc.vertical_fov, desc.aspect);
        projection.at(3, 2) = desc.near_clip;
        if (!is_finite(projection))
        {
            return false;
        }
        result = projection;
        return true;
    }
}
