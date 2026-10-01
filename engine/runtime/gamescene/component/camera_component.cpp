#include "gamescene/component/camera_component.h"

#include "logging/logger.h"
#include "gamescene/world/world.h"

namespace toy3d
{
    bool CameraComponent::is_valid_perspective(float vertical_fov_degrees, float near_clip, float far_clip)
    {
        return is_valid(CameraSettings{vertical_fov_degrees, near_clip, far_clip});
    }

    bool CameraComponent::set_camera_settings(const CameraSettings& settings)
    {
        if (!is_valid(settings))
        {
            TOY_LOG_ERROR("Camera settings require a finite, representable perspective projection.");
            return false;
        }
        if (settings_ == settings) return true;
        settings_ = settings;
        world().mark_content_changed();
        return true;
    }

    bool CameraComponent::set_perspective(float vertical_fov_degrees, float near_clip, float far_clip)
    {
        return set_camera_settings(CameraSettings{vertical_fov_degrees, near_clip, far_clip});
    }
} // namespace toy3d
