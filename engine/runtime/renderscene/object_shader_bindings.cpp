#include "renderscene/object_shader_bindings.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "math/matrix4.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"

#include <cstdint>
#include <unordered_map>

namespace toy3d
{
    RHIStatus create_object_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                            std::vector<ViewInfo>& view_infos)
    {
        if (!context.is_owned_by(device))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Object shader binding context is not owned by the injected device.");
        }

        std::unordered_map<const PrimitiveSceneProxy*, std::unordered_map<std::uint64_t, RHIBindingSetRef>>
            bindings_by_proxy_and_generation;
        for (const ViewInfo& view_info : view_infos)
        {
            for (const MeshBatch& mesh_batch : view_info.mesh_batches())
            {
                const PrimitiveSceneProxy* const proxy = &mesh_batch.scene_proxy();
                if (!is_finite(mesh_batch.object_shader_parameters().toy_object_to_world) ||
                    mesh_batch.object_data_generation() == 0u)
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Object shader binding requires finite current-frame Primitive canonical values.");
                }

                auto& bindings_by_generation = bindings_by_proxy_and_generation[proxy];
                const auto found = bindings_by_generation.find(mesh_batch.object_data_generation());
                if (found != bindings_by_generation.end())
                {
                    if (found->second && mesh_batch.object_binding() && found->second != mesh_batch.object_binding())
                    {
                        return RHIStatus::failure(
                            RHIErrorCode::InvalidArgument,
                            "Object shader binding found inconsistent frame-local data for one Primitive.");
                    }
                    if (!found->second && mesh_batch.object_binding())
                    {
                        found->second = mesh_batch.object_binding();
                    }
                }
                else
                {
                    bindings_by_generation.emplace(mesh_batch.object_data_generation(), mesh_batch.object_binding());
                }

                if (mesh_batch.object_binding() &&
                    (!mesh_batch.object_binding()->is_owned_by(device) ||
                     mesh_batch.object_binding()->group() != RHIBindingGroup::Object))
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Frame-local Object binding is incompatible with the injected device or logical group.");
                }
            }
        }

        for (ViewInfo& view_info : view_infos)
        {
            for (MeshBatch& mesh_batch : view_info.mesh_batches_)
            {
                RHIBindingSetRef& cached = bindings_by_proxy_and_generation.at(&mesh_batch.scene_proxy())
                                               .at(mesh_batch.object_data_generation());
                if (!cached)
                {
                    RHIResult<RHIBindingSetRef> created = create_transient_shader_binding(
                        device, context, mesh_batch.object_shader_parameters());
                    if (!created)
                    {
                        return created.status();
                    }
                    cached = std::move(created).value();
                }
            }
        }

        for (ViewInfo& view_info : view_infos)
        {
            for (MeshBatch& mesh_batch : view_info.mesh_batches_)
            {
                mesh_batch.publish_object_binding(bindings_by_proxy_and_generation.at(&mesh_batch.scene_proxy())
                                                      .at(mesh_batch.object_data_generation()));
            }
        }
        return RHIStatus::success();
    }
} // namespace toy3d
