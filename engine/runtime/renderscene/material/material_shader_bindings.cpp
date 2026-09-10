#include "renderscene/material/material_shader_bindings.h"

#include <cstddef>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"

namespace toy3d
{
    namespace
    {
        bool program_declares_material_group(const ShaderMapProgram& program)
        {
            for (const ShaderMapBinding& binding : program.data().bindings)
            {
                if (binding.group == RHIBindingGroup::Material)
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    RHIStatus create_material_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                              std::vector<ViewInfo>& view_infos)
    {
        for (std::size_t view_index = 0; view_index < view_infos.size(); ++view_index)
        {
            ViewInfo& view_info = view_infos[view_index];
            for (std::size_t batch_index = 0; batch_index < view_info.mesh_batches_.size(); ++batch_index)
            {
                MeshBatch& mesh_batch = view_info.mesh_batches_[batch_index];
                const ShaderMapProgramRef& program = mesh_batch.material_render_proxy().shader_program();
                if (!program || !program_declares_material_group(*program))
                {
                    mesh_batch.publish_material_binding(nullptr);
                    continue;
                }
                RHIResult<RHIBindingSetRef> materialized =
                    mesh_batch.material_render_proxy().materialize(device, context);
                if (!materialized)
                {
                    mesh_batch.publish_material_binding(nullptr);
                    TOY_LOG_ERROR("Material binding creation skipped View {} MeshBatch {}: {}", view_index,
                                  batch_index, materialized.status().message());
                    continue;
                }

                RHIBindingSetRef binding = std::move(materialized).value();
                if (!binding || binding->group() != RHIBindingGroup::Material)
                {
                    mesh_batch.publish_material_binding(nullptr);
                    TOY_LOG_ERROR("Material binding creation skipped View {} MeshBatch {} because the owner "
                                  "returned an incompatible logical binding.",
                                  view_index, batch_index);
                    continue;
                }
                mesh_batch.publish_material_binding(std::move(binding));
            }
        }
        return RHIStatus::success();
    }
} // namespace toy3d
