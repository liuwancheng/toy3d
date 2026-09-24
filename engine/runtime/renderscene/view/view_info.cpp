#include "renderscene/view/view_info.h"

#include <utility>

namespace toy3d
{
    ViewInfo::ViewInfo(SceneView scene_view, Matrix4 view_matrix, Matrix4 projection_matrix,
                       Matrix4 view_projection_matrix, Matrix4 inverse_view_matrix, Matrix4 inverse_projection_matrix,
                       Matrix4 inverse_view_projection_matrix, ConvexVolume view_frustum)
        : scene_view_(std::move(scene_view)), view_matrix_(std::move(view_matrix)),
          projection_matrix_(std::move(projection_matrix)), view_projection_matrix_(std::move(view_projection_matrix)),
          inverse_view_matrix_(std::move(inverse_view_matrix)),
          inverse_projection_matrix_(std::move(inverse_projection_matrix)),
          inverse_view_projection_matrix_(std::move(inverse_view_projection_matrix)),
          view_shader_parameters_{},
          view_frustum_(std::move(view_frustum))
    {
        view_shader_parameters_.toy_view = view_matrix_;
        view_shader_parameters_.toy_projection = projection_matrix_;
        view_shader_parameters_.toy_view_projection = view_projection_matrix_;
        view_shader_parameters_.toy_inverse_view = inverse_view_matrix_;
        view_shader_parameters_.toy_inverse_projection = inverse_projection_matrix_;
        view_shader_parameters_.toy_inverse_view_projection = inverse_view_projection_matrix_;
        view_shader_parameters_.toy_camera_position = scene_view_.camera_position();
        view_shader_parameters_.toy_camera_direction = scene_view_.camera_direction();
    }

    void ViewInfo::publish_view_binding(RHIBindingSetRef binding_set)
    {
        view_binding_ = std::move(binding_set);
    }
} // namespace toy3d
