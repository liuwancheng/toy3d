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
          mesh_(std::move(mesh)), render_data_(mesh_ ? mesh_->asset().geometry : SkeletalMeshAssetGeometry{}),
          deformation_(std::move(deformation))
    {
    }

    RHIStatus SkeletalMeshSceneProxy::make_bone_buffers(const SkeletalMeshDeformationData& deformation,
                                                        std::vector<std::unique_ptr<BoneMatrixBuffer>>& buffers) const
    {
        if (!mesh_ || !deformation.bone_layout || !mesh_->bone_layout()->compatible(*deformation.bone_layout) ||
            !deformation.pose_revision || !deformation.has_mesh_bounds)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skeletal pose does not match its mesh.");
        }
        for (const auto& map : render_data_.section_bone_maps())
        {
            const auto rows = build_bone_matrix_rows(deformation, map);
            if (!rows.succeeded())
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, rows.status().message);
            }
            buffers.push_back(std::make_unique<BoneMatrixBuffer>(rows.value()));
        }
        return RHIStatus::success();
    }

    RHIStatus SkeletalMeshSceneProxy::begin_init_resources(RenderResourceManager& manager)
    {
        if (!deformation_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skeletal mesh has no pose.");
        }
        auto status = render_data_.begin_init(manager);
        if (!status)
        {
            return status;
        }
        status = make_bone_buffers(*deformation_, bone_buffers_);
        if (!status)
        {
            return status;
        }
        for (auto& buffer : bone_buffers_)
        {
            status = manager.begin_init(*buffer);
            if (!status)
            {
                return status;
            }
        }
        return status;
    }

    RHIStatus SkeletalMeshSceneProxy::set_deformation(std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                                                      RenderResourceManager& manager)
    {
        if (!deformation || (deformation_ && deformation->pose_revision <= deformation_->pose_revision))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Stale or missing skeletal pose.");
        }
        std::vector<std::unique_ptr<BoneMatrixBuffer>> candidate;
        auto status = make_bone_buffers(*deformation, candidate);
        if (!status)
        {
            return status;
        }
        for (auto& buffer : candidate)
        {
            status = manager.begin_init(*buffer);
            if (!status)
            {
                for (auto& initialized : candidate)
                {
                    manager.release(*initialized);
                }
                return status;
            }
        }
        // RHI refs already captured by command lists outlive these RenderResource owners.
        for (auto& buffer : bone_buffers_)
        {
            status = manager.release(*buffer);
            if (!status)
            {
                for (auto& initialized : candidate)
                {
                    manager.release(*initialized);
                }
                return status;
            }
        }
        bone_buffers_ = std::move(candidate);
        deformation_ = std::move(deformation);
        return RHIStatus::success();
    }

    RHIStatus SkeletalMeshSceneProxy::release_resources(RenderResourceManager& manager, bool)
    {
        RHIStatus status;
        for (auto& buffer : bone_buffers_)
        {
            const auto released = manager.release(*buffer);
            if (status && !released)
            {
                status = released;
            }
        }
        bone_buffers_.clear();
        const auto released = render_data_.release(manager);
        return status ? released : status;
    }

    bool SkeletalMeshSceneProxy::resources_drawable() const
    {
        if (!render_data_.is_drawable() || bone_buffers_.size() != render_data_.sections().size())
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
        return render_data_.sections().size();
    }

    std::uint64_t SkeletalMeshSceneProxy::pose_revision() const
    {
        return deformation_ ? deformation_->pose_revision : 0;
    }

    RHIStatus SkeletalMeshSceneProxy::collect_mesh_batches(std::vector<MeshBatch>& batches) const
    {
        auto status = render_data_.prepare_current_recording();
        if (!status)
        {
            return status;
        }
        if (!resources_drawable())
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Skeletal mesh buffers are not drawable.");
        }
        std::vector<MeshBatch> candidate;
        const auto& sections = render_data_.sections();
        const auto& materials = material_render_proxies();
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto& section = sections[i];
            if (section.material_slot >= materials.size() || !materials[section.material_slot])
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Skeletal mesh material slot is missing.");
            }
            candidate.emplace_back(*this, *render_data_.vertex_factory(), render_data_.index_buffer_binding(),
                                   *materials[section.material_slot], section.first_index, section.index_count,
                                   static_cast<std::uint32_t>(i), bone_buffers_[i]->view(),
                                   render_data_.num_bone_influences(), render_data_.has_valid_tangent_frame());
        }
        batches.insert(batches.end(), candidate.begin(), candidate.end());
        return RHIStatus::success();
    }
} // namespace toy3d
