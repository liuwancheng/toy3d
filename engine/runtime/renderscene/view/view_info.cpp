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
          view_shader_parameters_{view_matrix_,
                                  projection_matrix_,
                                  view_projection_matrix_,
                                  inverse_view_matrix_,
                                  inverse_projection_matrix_,
                                  inverse_view_projection_matrix_,
                                  scene_view_.camera_position(),
                                  scene_view_.camera_direction()},
          view_frustum_(std::move(view_frustum))
    {
    }

    void ViewInfo::publish_view_binding(RHIBindingSetRef binding_set)
    {
        view_binding_ = std::move(binding_set);
    }
} // namespace toy3d
