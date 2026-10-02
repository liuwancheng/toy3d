#pragma once

#include "gamescene/component/scene_component.h"
#include "asset/scene/component_settings.h"
#include "rendercore/view/scene_view.h"

namespace toy3d
{
    class CameraComponent final : public SceneComponent
    {
      public:
        explicit CameraComponent(Actor& owner) : SceneComponent(owner)
        {
        }
        ~CameraComponent() override = default;

        CameraProjectionMode projection_mode() const
        {
            return projection_mode_;
        }
        float vertical_fov_degrees() const
        {
            return settings_.vertical_fov;
        }
        float near_clip() const
        {
            return settings_.near_clip;
        }
        float far_clip() const
        {
            return settings_.far_clip;
        }

        const CameraSettings& camera_settings() const
        {
            return settings_;
        }
        bool set_camera_settings(const CameraSettings& settings);

        bool set_perspective(float vertical_fov_degrees, float near_clip, float far_clip);
        // Reference-aspect projection must be representable before publishing
        // settings. View construction still validates the actual output aspect.
        static bool is_valid_perspective(float vertical_fov_degrees, float near_clip, float far_clip);

      private:
        CameraProjectionMode projection_mode_ = CameraProjectionMode::Perspective;
        CameraSettings settings_;
    };
} // namespace toy3d
