#include "renderscene/view/view_shader_bindings.h"

#include <cstddef>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    RHIStatus create_view_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                          std::vector<ViewInfo>& view_infos)
    {
        // Validate every canonical CPU value before the first transient upload,
        // so deterministic View failures cannot leave partial GPU work.
        for (const ViewInfo& view_info : view_infos)
        {
            const ViewShaderParameters& parameters = view_info.view_shader_parameters();
            if (!is_finite(parameters.toy_view) || !is_finite(parameters.toy_projection) ||
                !is_finite(parameters.toy_view_projection) || !is_finite(parameters.toy_inverse_view) ||
                !is_finite(parameters.toy_inverse_projection) || !is_finite(parameters.toy_inverse_view_projection) ||
                !is_finite(parameters.toy_camera_position) || !is_finite(parameters.toy_camera_direction))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "View Shader parameters contain non-finite canonical values");
            }
        }

        std::vector<RHIBindingSetRef> created_bindings(view_infos.size());
        for (std::size_t view_index = 0; view_index < view_infos.size(); ++view_index)
        {
            ViewInfo& view_info = view_infos[view_index];
            if (view_info.mesh_batches().empty())
            {
                continue;
            }
            if (view_info.view_binding())
            {
                created_bindings[view_index] = view_info.view_binding();
                continue;
            }

            RHIResult<RHIBindingSetRef> binding =
                create_transient_shader_binding(device, context, view_info.view_shader_parameters());
            if (!binding)
            {
                return binding.status();
            }
            created_bindings[view_index] = std::move(binding).value();
        }

        for (std::size_t view_index = 0; view_index < view_infos.size(); ++view_index)
        {
            view_infos[view_index].publish_view_binding(std::move(created_bindings[view_index]));
        }
        return RHIStatus::success();
    }

} // namespace toy3d
