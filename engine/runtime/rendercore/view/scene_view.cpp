#include "rendercore/view/scene_view.h"

#include <utility>

#include "rendercore/scene_interface.h"

namespace toy3d
{
    SceneView::SceneView(Vector3 camera_position, Quaternion camera_orientation, Vector3 camera_direction,
                         IntRect view_rect, Extent output_extent,
                         CameraProjectionMode projection_mode, Radians vertical_fov, float near_clip, float far_clip)
        : camera_position_(camera_position), camera_orientation_(camera_orientation),
          camera_direction_(camera_direction), view_rect_(view_rect), output_extent_(output_extent),
          projection_mode_(projection_mode), vertical_fov_(vertical_fov),
          near_clip_(near_clip), far_clip_(far_clip)
    {
    }

    SceneViewFamily::SceneViewFamily(SceneInterface& scene_interface, Extent output_extent,
                                     std::vector<SceneView> views)
        : scene_interface_(&scene_interface), output_extent_(output_extent), views_(std::move(views))
    {
    }
} // namespace toy3d
