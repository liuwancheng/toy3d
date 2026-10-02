#include "renderscene/material/material_shader_bindings.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "rendercore/geometry/vertex_factory.h"
#include "rendercore/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    RHIStatus create_material_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                              std::vector<ViewInfo>& view_infos)
    {
        std::vector<MeshBatch*> batches;
        for (auto& view : view_infos)
        {
            for (auto& batch : view.mesh_batches_)
            {
                batches.push_back(&batch);
            }
            for (auto& cascade : view.shadow_cascades_)
            {
                for (auto& batch : cascade.batches)
                {
                    batches.push_back(&batch);
                }
            }
        }
        for (auto* batch : batches)
        {
            const auto forward = batch->material_program();
            if (!forward.succeeded())
            {
                return RHIStatus::failure(RHIErrorCode::Unsupported, forward.error);
            }
            const auto& shader_map = batch->material_render_proxy().shader_map();
            const bool needed =
                std::any_of(shader_map->programs().begin(), shader_map->programs().end(),
                            [batch](const ShaderMapProgramRef& program)
                            {
                                return program->data().contract.vertex_factory == batch->vertex_factory().type() &&
                                       std::any_of(program->data().bindings.begin(), program->data().bindings.end(),
                                                   [](const ShaderMapBinding& binding)
                                                   {
                                                       return binding.group == RHIBindingGroup::Material;
                                                   });
                            });
            if (!needed)
            {
                batch->publish_material_binding(nullptr);
                continue;
            }
            // The Proxy owns one complete logical Material binding for all roles.
            // Camera and offscreen shadow batches snapshot the same cached owner.
            auto materialized = batch->material_render_proxy().materialize(device, context);
            if (!materialized)
            {
                batch->publish_material_binding(nullptr);
                TOY_LOG_ERROR("Material binding preparation failed: {}", materialized.status().message());
                continue;
            }
            auto binding = std::move(materialized).value();
            if (!binding || binding->group() != RHIBindingGroup::Material)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Material owner returned an incompatible logical binding.");
            }
            batch->publish_material_binding(std::move(binding));
        }
        return RHIStatus::success();
    }
} // namespace toy3d
