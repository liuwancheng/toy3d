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
        std::vector<RHIBufferRef> prepared_buffers(view_infos.size());
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
            if (view_info.view_uniform_buffer())
            {
                prepared_buffers[view_index] = view_info.view_uniform_buffer();
                continue;
            }

            RHIResult<RHIBufferRef> buffer = create_view_uniform_shader_buffer(
                device, context, view_info.view_uniform_shader_parameters());
            if (!buffer)
            {
                return buffer.status();
            }
            prepared_buffers[view_index] = std::move(buffer).value();
        }

        for (std::size_t view_index = 0; view_index < view_infos.size(); ++view_index)
        {
            view_infos[view_index].publish_view_uniform_buffer(std::move(prepared_buffers[view_index]));
        }
        return RHIStatus::success();
    }

    RHIResult<RHIBindingSetRef> resolve_view_uniform_binding(RHIDevice& device, const ViewInfo& view_info,
                                                             const RHIBindingLayoutRef& binding_layout,
                                                             const ShaderMapProgram& shader_program)
    {
        if (!view_info.view_uniform_buffer())
        {
            return RHIResult<RHIBindingSetRef>::failure(
                RHIErrorCode::InvalidArgument, "View uniform buffer was not prepared before mesh-pass preparation");
        }
        const RHIStatus schema_status = validate_view_uniform_shader_program(shader_program);
        if (!schema_status)
        {
            return RHIResult<RHIBindingSetRef>::failure(schema_status.code(), schema_status.message());
        }
        RHIBindingSetRef cached_binding = view_info.find_view_binding_adapter(binding_layout);
        if (cached_binding)
        {
            return RHIResult<RHIBindingSetRef>::success(std::move(cached_binding));
        }

        RHIResult<RHIBindingSetRef> binding = create_view_uniform_shader_binding(
            device, binding_layout, shader_program, view_info.view_uniform_buffer());
        if (!binding)
        {
            return binding;
        }
        view_info.add_view_binding_adapter(binding_layout, binding.value());
        return binding;
    }
} // namespace toy3d
