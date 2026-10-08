#include "rendercore/scene/skeletal_mesh_scene_proxy.h"

#include <utility>

#include "rendercore/material/material_render_proxy.h"
#include "rendercore/render_resource_manager.h"
#include "renderscene/mesh_batch.h"

namespace toy3d
{
    SkeletalMeshSceneProxy::SkeletalMeshSceneProxy(Matrix4 transform, AxisAlignedBounds bounds, bool visible,
                                                   SkeletalMeshRef mesh,
                                                   std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                                                   std::vector<MaterialRenderProxy*> materials, std::uint32_t actor_id,
                                                   std::uint32_t component_id, bool cast_shadows, bool receives_shadows)
        : PrimitiveSceneProxy(std::move(transform), bounds, visible, actor_id, component_id, cast_shadows,
                              receives_shadows, std::move(materials)),
          mesh_(std::move(mesh)), deformation_(std::move(deformation))
    {
    }

    RHIStatus SkeletalMeshSceneProxy::make_bone_buffers(const SkeletalMeshDeformationData& deformation,
                                                        std::vector<std::shared_ptr<BoneMatrixBuffer>>& buffers) const
    {
        if (!mesh_ || !deformation.bone_layout || !mesh_->bone_layout()->compatible(*deformation.bone_layout) ||
            !deformation.pose_revision || !deformation.has_mesh_bounds)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skeletal pose does not match its mesh.");
        }
        for (const auto& map : render_data_->section_bone_maps())
        {
            const auto rows = build_bone_matrix_rows(deformation, map);
            if (!rows.succeeded())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, rows.status().message);
            }
            buffers.push_back(std::make_shared<BoneMatrixBuffer>(rows.value()));
        }
        return RHIStatus::success();
    }

    RHIStatus SkeletalMeshSceneProxy::begin_init_resources(RenderResourceManager& manager)
    {
        if (!mesh_ || !deformation_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skeletal mesh has no geometry or pose.");
        }
        auto acquired = manager.acquire(*mesh_->render_data());
        if (!acquired)
        {
            return acquired.status();
        }
        render_data_ = std::move(acquired).value();
        // Initial pose and later replacements use the same transactional path.
        auto initial_pose = std::move(deformation_);
        return set_deformation(std::move(initial_pose), manager);
    }

    RHIStatus SkeletalMeshSceneProxy::set_deformation(std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                                                      RenderResourceManager& manager)
    {
        if (!render_data_ || !deformation ||
            (deformation_ && deformation->pose_revision <= deformation_->pose_revision))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Stale or missing skeletal pose.");
        }
        std::vector<std::shared_ptr<BoneMatrixBuffer>> buffers;
        auto status = make_bone_buffers(*deformation, buffers);
        if (!status)
        {
            return status;
        }
        std::vector<RenderResourceRef<BoneMatrixBuffer>> candidate;
        for (const auto& buffer : buffers)
        {
            auto acquired = manager.acquire(*buffer);
            if (!acquired)
            {
                return acquired.status();
            }
            candidate.push_back(std::move(acquired).value());
        }
        bone_buffers_ = std::move(candidate);
        deformation_ = std::move(deformation);
        return RHIStatus::success();
    }

    bool SkeletalMeshSceneProxy::resources_drawable() const
    {
        if (!render_data_ || !render_data_->is_drawable() || bone_buffers_.size() != render_data_->sections().size())
        {
            return false;
        }
        for (const auto& buffer : bone_buffers_)
        {
            if (!buffer->view())
            {
                return false;
            }
        }
        return true;
    }

    std::size_t SkeletalMeshSceneProxy::mesh_section_count() const
    {
        return render_data_ ? render_data_->sections().size() : 0;
    }

    std::uint64_t SkeletalMeshSceneProxy::pose_revision() const
    {
        return deformation_ ? deformation_->pose_revision : 0;
    }

    RHIStatus SkeletalMeshSceneProxy::collect_mesh_batches(std::vector<MeshBatch>& batches) const
    {
        if (!render_data_)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Skeletal geometry has not been acquired.");
        }
        auto status = render_data_->prepare_current_recording();
        if (!status)
        {
            return status;
        }
        if (!resources_drawable())
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Skeletal mesh buffers are not drawable.");
        }
        std::vector<MeshBatch> candidate;
        const auto& sections = render_data_->sections();
        const auto& materials = material_render_proxies();
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto& section = sections[i];
            if (section.material_slot >= materials.size() || !materials[section.material_slot])
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skeletal mesh material slot is missing.");
            }
            candidate.emplace_back(*this, *render_data_->vertex_factory(), render_data_->index_buffer_binding(),
                                   *materials[section.material_slot], section.first_index, section.index_count,
                                   static_cast<std::uint32_t>(i), bone_buffers_[i]->view(),
                                   render_data_->num_bone_influences(), render_data_->has_valid_tangent_frame());
        }
        batches.insert(batches.end(), candidate.begin(), candidate.end());
        return RHIStatus::success();
    }
} // namespace toy3d
