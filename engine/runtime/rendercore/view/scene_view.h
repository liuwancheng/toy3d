#pragma once

#include "math/angle.h"
#include "math/integer_vector.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "math/vector4.h"

#include <vector>

namespace toy3d
{
    class SceneInterface;

    // Owned world-space debug geometry travels with the view request, never a component pointer.
    struct DebugLineVertex
    {
        Vector4 position{0, 0, 0, 1};
        Vector4 color;
    };

    // CameraProjectionMode is a copied value shared with CameraComponent; it
    // does not grant Render-side access to the Game-side Camera object.
    enum class CameraProjectionMode
    {
        Perspective,
        PerspectiveInfiniteFar,
        Orthographic,
        Custom
    };

    // Game-side one-frame value. Camera and viewport state is copied before
    // ownership crosses to the logical Rendering Thread, where it is read-only.
    class SceneView
    {
      public:
        SceneView(Vector3 camera_position, Quaternion camera_orientation, Vector3 camera_direction, IntRect view_rect,
                  Extent output_extent, CameraProjectionMode projection_mode, Radians vertical_fov, float near_clip,
                  float far_clip);

        const Vector3& camera_position() const
        {
            return camera_position_;
        }
        const Quaternion& camera_orientation() const
        {
            return camera_orientation_;
        }
        const Vector3& camera_direction() const
        {
            return camera_direction_;
        }
        const IntRect& view_rect() const
        {
            return view_rect_;
        }
        Extent output_extent() const
        {
            return output_extent_;
        }
        CameraProjectionMode projection_mode() const
        {
            return projection_mode_;
        }
        Radians vertical_fov() const
        {
            return vertical_fov_;
        }
        float near_clip() const
        {
            return near_clip_;
        }
        float far_clip() const
        {
            return far_clip_;
        }
        bool infinite_far() const
        {
            return projection_mode_ == CameraProjectionMode::PerspectiveInfiniteFar;
        }

      private:
        Vector3 camera_position_;
        Quaternion camera_orientation_;
        Vector3 camera_direction_;
        IntRect view_rect_;
        Extent output_extent_;
        CameraProjectionMode projection_mode_ = CameraProjectionMode::Perspective;
        Radians vertical_fov_;
        float near_clip_ = 0.0f;
        float far_clip_ = 0.0f;
    };

    // Game-side one-shot input transferred to SceneRenderer. SceneInterface is
    // a non-owning stable bridge whose owner must outlive the accepted Draw.
    class SceneViewFamily
    {
      public:
        SceneViewFamily(SceneInterface& scene_interface, Extent output_extent, std::vector<SceneView> views);

        SceneViewFamily(const SceneViewFamily&) = delete;
        SceneViewFamily& operator=(const SceneViewFamily&) = delete;
        SceneViewFamily(SceneViewFamily&&) = default;
        SceneViewFamily& operator=(SceneViewFamily&&) = default;
        ~SceneViewFamily() = default;

        SceneInterface& scene_interface() const
        {
            return *scene_interface_;
        }
        Extent output_extent() const
        {
            return output_extent_;
        }
        const std::vector<SceneView>& views() const
        {
            return views_;
        }

      private:
        SceneInterface* scene_interface_ = nullptr;
        Extent output_extent_;
        std::vector<SceneView> views_;
    };
} // namespace toy3d
