#include "rendercore/view/scene_view.h"

#include <utility>

#include "rendercore/scene_interface.h"

namespace toy3d
{
    SceneView::SceneView(Vector3 camera_position, Quaternion camera_orientation, Vector3 camera_direction,
                         UIntVector2 view_rect_minimum, UIntVector2 view_rect_size, UIntVector2 output_size,
                         CameraProjectionMode projection_mode, Radians vertical_fov, float near_clip, float far_clip)
        : camera_position_(camera_position), camera_orientation_(camera_orientation),
          camera_direction_(camera_direction), view_rect_minimum_(view_rect_minimum), view_rect_size_(view_rect_size),
          output_size_(output_size), projection_mode_(projection_mode), vertical_fov_(vertical_fov),
          near_clip_(near_clip), far_clip_(far_clip)
    {
    }

    SceneViewFamily::SceneViewFamily(SceneInterface& scene_interface, UIntVector2 output_size,
                                     std::vector<SceneView> views)
        : scene_interface_(&scene_interface), output_size_(output_size), views_(std::move(views))
    {
    }
} // namespace toy3d
