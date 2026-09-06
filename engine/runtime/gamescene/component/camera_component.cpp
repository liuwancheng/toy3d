#include "gamescene/component/camera_component.h"

#include "logging/logger.h"
#include "math/scalar_math.h"

namespace toy3d
{
    bool CameraComponent::set_perspective(float vertical_fov_degrees, float near_clip, float far_clip)
    {
        if (!is_finite(vertical_fov_degrees) || !is_finite(near_clip) || !is_finite(far_clip) ||
            vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 180.0f || near_clip <= 0.0f ||
            far_clip <= near_clip)
        {
            TOY_LOG_ERROR("Perspective camera requires 0 < FOV < 180 and 0 < near < far.");
            return false;
        }

        projection_mode_ = CameraProjectionMode::Perspective;
        vertical_fov_degrees_ = vertical_fov_degrees;
        near_clip_ = near_clip;
        far_clip_ = far_clip;
        return true;
    }
} // namespace toy3d
