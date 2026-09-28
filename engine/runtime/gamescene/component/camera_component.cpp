#include "gamescene/component/camera_component.h"

#include "logging/logger.h"
#include "math/geometry/convex_volume.h"
#include "math/matrix_construction.h"
#include "math/scalar_math.h"

namespace toy3d
{
    bool CameraComponent::is_valid_perspective(float vertical_fov_degrees, float near_clip, float far_clip)
    {
        if (!is_finite(vertical_fov_degrees) || !is_finite(near_clip) || !is_finite(far_clip) ||
            vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 180.0f || near_clip <= 0.0f ||
            far_clip <= near_clip)
            return false;
        PerspectiveProjectionDesc desc;
        desc.vertical_fov = to_radians(Degrees(vertical_fov_degrees));
        desc.near_clip = near_clip;
        desc.far_clip = far_clip;
        Matrix4 projection;
        Matrix4 inverse_projection;
        ConvexVolume frustum;
        return try_make_perspective_projection(desc, projection) && try_inverse(projection, inverse_projection) &&
               try_make_reversed_z_frustum(projection, false, frustum);
    }

    bool CameraComponent::set_perspective(float vertical_fov_degrees, float near_clip, float far_clip)
    {
        if (!is_valid_perspective(vertical_fov_degrees, near_clip, far_clip))
        {
            TOY_LOG_ERROR("Perspective camera requires a representable projection with 0 < FOV < 180 and 0 < near < far.");
            return false;
        }

        projection_mode_ = CameraProjectionMode::Perspective;
        vertical_fov_degrees_ = vertical_fov_degrees;
        near_clip_ = near_clip;
        far_clip_ = far_clip;
        return true;
    }
} // namespace toy3d
