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
          view_uniform_shader_parameters_{view_matrix_,
                                          projection_matrix_,
                                          view_projection_matrix_,
                                          inverse_view_matrix_,
                                          inverse_projection_matrix_,
                                          inverse_view_projection_matrix_,
                                          scene_view_.camera_position(),
                                          0.0f,
                                          scene_view_.camera_direction(),
                                          0.0f},
          view_frustum_(std::move(view_frustum))
    {
    }

    void ViewInfo::publish_view_uniform_buffer(RHIBufferRef buffer)
    {
        if (buffer != view_uniform_buffer_)
        {
            view_binding_adapters_.clear();
        }
        view_uniform_buffer_ = std::move(buffer);
    }

    RHIBindingSetRef ViewInfo::find_view_binding_adapter(const RHIBindingLayoutRef& binding_layout) const
    {
        for (const std::pair<RHIBindingLayoutRef, RHIBindingSetRef>& adapter : view_binding_adapters_)
        {
            if (adapter.first && binding_layout && adapter.first->desc() == binding_layout->desc())
            {
                return adapter.second;
            }
        }
        return nullptr;
    }

    void ViewInfo::add_view_binding_adapter(RHIBindingLayoutRef binding_layout, RHIBindingSetRef binding_set) const
    {
        view_binding_adapters_.emplace_back(std::move(binding_layout), std::move(binding_set));
    }
} // namespace toy3d
