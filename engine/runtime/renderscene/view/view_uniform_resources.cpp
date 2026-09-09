#include "renderscene/view/view_uniform_resources.h"

#include <cstddef>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    RHIStatus prepare_view_uniform_resources(RHIDevice& device, RHICommandContext& context,
                                             std::vector<ViewInfo>& view_infos)
    {
        if (!context.is_owned_by(device))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "View uniform preparation context is not owned by the injected device");
        }
        std::vector<RHIUniformBufferSlice> prepared_slices(view_infos.size());
        std::vector<RHIBindingSetRef> prepared_bindings(view_infos.size());
        for (std::size_t view_index = 0; view_index < view_infos.size(); ++view_index)
        {
            ViewInfo& view_info = view_infos[view_index];
            bool requires_view_uniform = false;
            for (const MeshBatch& mesh_batch : view_info.mesh_batches())
            {
                const ShaderMapProgramRef& shader_program = mesh_batch.material_render_proxy().shader_program();
                if (!shader_program)
                {
                    continue;
                }
                for (const ShaderMapBinding& binding : shader_program->data().bindings)
                {
                    if (binding.group == RHIBindingGroup::View)
                    {
                        requires_view_uniform = true;
                        break;
                    }
                }
                if (requires_view_uniform)
                {
                    break;
                }
            }

            if (!requires_view_uniform)
            {
                continue;
            }
            if (view_info.view_binding_set())
            {
                prepared_slices[view_index] = view_info.view_uniform_slice();
                prepared_bindings[view_index] = view_info.view_binding_set();
                continue;
            }

            RHIResult<RHIUniformBufferSlice> slice = upload_view_uniform_shader_parameters(
                context, view_info.view_uniform_shader_parameters());
            if (!slice)
            {
                return slice.status();
            }
            RHIResult<RHIBindingSetRef> binding = create_view_uniform_shader_binding(device, slice.value());
            if (!binding)
            {
                return binding.status();
            }
            prepared_slices[view_index] = std::move(slice).value();
            prepared_bindings[view_index] = std::move(binding).value();
        }

        for (std::size_t view_index = 0; view_index < view_infos.size(); ++view_index)
        {
            view_infos[view_index].publish_view_uniform_resources(std::move(prepared_slices[view_index]),
                                                                  std::move(prepared_bindings[view_index]));
        }
        return RHIStatus::success();
    }

} // namespace toy3d
