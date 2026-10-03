#include "renderscene/material/material_shader_bindings.h"

#include <algorithm>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    namespace
    {
        RHIStatus prepare_material_program(RHIDevice& device, RHICommandContext& context, MeshBatch& batch,
                                           const ShaderMapProgramResult& selected)
        {
            if (!selected.succeeded())
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported, selected.error);
            }
            const auto materialized = batch.material_render_proxy().materialize(device, context, *selected.program);
            if (!materialized)
            {
                batch.publish_material_binding(selected.program, nullptr);
                TOY_LOG_ERROR("Material binding preparation failed for {}: {}", selected.program->data().pass_name,
                              materialized.status().message());
                return RHIStatus::success();
            }
            const auto& binding = materialized.value();
            if (binding && binding->group() != RHIBindingGroup::Material)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Material owner returned an incompatible active binding");
            }
            batch.publish_material_binding(selected.program, binding);
            return RHIStatus::success();
        }

        RHIStatus prepare_material_role(RHIDevice& device, RHICommandContext& context, MeshBatch& batch,
                                        shader::ShaderPassRole role)
        {
            const auto& map = batch.material_render_proxy().shader_map();
            if (!map)
            {
                return RHIStatus::failure(RHIErrorCode::NotReady, "Material ShaderMap collection is missing");
            }
            if (std::none_of(map->index().passes.begin(), map->index().passes.end(),
                             [role](const auto& pass)
                             {
                                 return pass.role == role;
                             }))
            {
                // Opaque engine default mesh roles have no Material bindings.
                return RHIStatus::success();
            }
            return prepare_material_program(device, context, batch, batch.find_program(*map, role));
        }
    } // namespace

    RHIStatus create_material_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                              std::vector<ViewInfo>& view_infos)
    {
        for (auto& view : view_infos)
        {
            for (auto& batch : view.mesh_batches_)
            {
                auto status = prepare_material_program(
                    device, context, batch, batch.material_program(view.shadow_active(), view.environment_active()));
                if (status)
                {
                    status = prepare_material_role(device, context, batch, shader::ShaderPassRole::HitProxy);
                }
                if (!status)
                {
                    return status;
                }
            }
            for (auto& cascade : view.shadow_cascades_)
            {
                for (auto& batch : cascade.batches)
                {
                    const auto status =
                        prepare_material_role(device, context, batch, shader::ShaderPassRole::ShadowDepth);
                    if (!status)
                    {
                        return status;
                    }
                }
            }
        }
        return RHIStatus::success();
    }
} // namespace toy3d
