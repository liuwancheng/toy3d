#pragma once

#include "gamescene/component/scene_component.h"
#include "rendercore/view/scene_view.h"

namespace toy3d
{
    class CameraComponent final : public SceneComponent
    {
      public:
        explicit CameraComponent(Actor& owner) : SceneComponent(owner) {}
        ~CameraComponent() override = default;

        CameraProjectionMode projection_mode() const { return projection_mode_; }
        float vertical_fov_degrees() const { return vertical_fov_degrees_; }
        float near_clip() const { return near_clip_; }
        float far_clip() const { return far_clip_; }

        bool set_perspective(float vertical_fov_degrees, float near_clip, float far_clip);
        // Reference-aspect projection must be representable before publishing
        // settings. View construction still validates the actual output aspect.
        static bool is_valid_perspective(float vertical_fov_degrees, float near_clip, float far_clip);

      private:
        CameraProjectionMode projection_mode_ = CameraProjectionMode::Perspective;
        float vertical_fov_degrees_ = 60.0f;
        float near_clip_ = 0.1f;
        float far_clip_ = 1000.0f;
    };
} // namespace toy3d
