#include "renderscene/object_shader_bindings.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "math/matrix4.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/mesh_batch.h"
#include "renderscene/view/view_info.h"

#include <cstdint>
#include <unordered_map>
#include <map>
#include <utility>
#include <vector>

namespace toy3d
{
    RHIStatus create_object_shader_bindings(RHIDevice& device, RHICommandContext& context,
                                            std::vector<ViewInfo>& view_infos)
    {
        std::unordered_map<const PrimitiveSceneProxy*,
                           std::map<std::pair<std::uint64_t, std::uint32_t>, RHIBindingSetRef>>
            bindings_by_proxy_and_generation;
        std::vector<MeshBatch*> batches;
        for (ViewInfo& view_info : view_infos)
        {
            for (MeshBatch& batch : view_info.mesh_batches_)
            {
                batches.push_back(&batch);
            }
            for (ShadowCascadeInfo& cascade : view_info.shadow_cascades_)
            {
                for (MeshBatch& batch : cascade.batches)
                {
                    batches.push_back(&batch);
                }
            }
        }
        for (const MeshBatch* mesh_batch_ptr : batches)
        {
            const MeshBatch& mesh_batch = *mesh_batch_ptr;
            const PrimitiveSceneProxy* const proxy = &mesh_batch.scene_proxy();
            if (!is_finite(mesh_batch.object_shader_parameters().toy_object_to_world) ||
                !is_finite(mesh_batch.object_shader_parameters().toy_object_normal_to_world) ||
                mesh_batch.object_data_generation() == 0u)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Object shader binding requires finite current-frame Primitive canonical values.");
            }

            auto& bindings_by_generation = bindings_by_proxy_and_generation[proxy];
            const auto found = bindings_by_generation.find(std::make_pair(
                mesh_batch.object_data_generation(), mesh_batch.bone_matrices() ? mesh_batch.section_index() : 0u));
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
                bindings_by_generation.emplace(
                    std::make_pair(mesh_batch.object_data_generation(),
                                   mesh_batch.bone_matrices() ? mesh_batch.section_index() : 0u),
                    mesh_batch.object_binding());
            }

            if (mesh_batch.object_binding() && (!mesh_batch.object_binding()->is_owned_by(device) ||
                                                mesh_batch.object_binding()->group() != RHIBindingGroup::Object))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Frame-local Object binding is incompatible with the injected device or logical group.");
            }
        }

        for (MeshBatch* mesh_batch_ptr : batches)
        {
            MeshBatch& mesh_batch = *mesh_batch_ptr;
            RHIBindingSetRef& cached =
                bindings_by_proxy_and_generation.at(&mesh_batch.scene_proxy())
                    .at(std::make_pair(mesh_batch.object_data_generation(),
                                       mesh_batch.bone_matrices() ? mesh_batch.section_index() : 0u));
            if (!cached)
            {
                const auto& object = mesh_batch.object_shader_parameters();
                RHIResult<RHIBindingSetRef> created =
                    RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::NotReady, "Object binding is not created.");
                if (mesh_batch.bone_matrices())
                {
                    if (object.toy_num_bone_influences != 4 && object.toy_num_bone_influences != 8)
                    {
                        return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid skin influence width.");
                    }
                    GPUSkinObjectShaderParameters skin;
                    skin.toy_object_to_world = object.toy_object_to_world;
                    skin.toy_object_normal_to_world = object.toy_object_normal_to_world;
                    skin.toy_receives_shadows = object.toy_receives_shadows;
                    skin.toy_num_bone_influences = object.toy_num_bone_influences;
                    skin.toy_bone_matrices = mesh_batch.bone_matrices();
                    created = create_transient_shader_binding(device, context, skin);
                }
                else
                {
                    created = create_transient_shader_binding(device, context, object);
                }
                if (!created)
                {
                    return created.status();
                }
                cached = std::move(created).value();
            }
        }

        for (MeshBatch* mesh_batch_ptr : batches)
        {
            MeshBatch& mesh_batch = *mesh_batch_ptr;
            mesh_batch.publish_object_binding(
                bindings_by_proxy_and_generation.at(&mesh_batch.scene_proxy())
                    .at(std::make_pair(mesh_batch.object_data_generation(),
                                       mesh_batch.bone_matrices() ? mesh_batch.section_index() : 0u)));
        }
        return RHIStatus::success();
    }
} // namespace toy3d
