#pragma once

#include "rendercore/geometry/bone_matrix_buffer.h"
#include "rendercore/geometry/skeletal_mesh.h"
#include "rendercore/geometry/skeletal_mesh_deformation.h"
#include "rendercore/geometry/skeletal_mesh_render_data.h"
#include "rendercore/scene/primitive_scene_proxy.h"

namespace toy3d
{
    // RT owns mutable geometry and immutable section uploads, independently of GT mesh state.
    class SkeletalMeshSceneProxy final : public PrimitiveSceneProxy
    {
      public:
        SkeletalMeshSceneProxy(Matrix4 transform, AxisAlignedBounds bounds, bool visible, SkeletalMeshRef mesh,
                               std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                               std::vector<MaterialRenderProxy*> materials, std::uint32_t actor_id,
                               std::uint32_t component_id, bool cast_shadows, bool receives_shadows);
        ~SkeletalMeshSceneProxy() override = default;
        RHIStatus begin_init_resources(RenderResourceManager& manager) override;
        RHIStatus release_resources(RenderResourceManager& manager, bool release_shared_geometry) override;
        bool resources_drawable() const override;
        std::size_t mesh_section_count() const override;
        RHIStatus collect_mesh_batches(std::vector<MeshBatch>& batches) const override;
        RHIStatus set_deformation(std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                                  RenderResourceManager& manager);
        std::uint64_t pose_revision() const;

      private:
        RHIStatus make_bone_buffers(const SkeletalMeshDeformationData& deformation,
                                    std::vector<std::unique_ptr<BoneMatrixBuffer>>& buffers) const;
        SkeletalMeshRef mesh_;
        mutable SkeletalMeshRenderData render_data_;
        std::shared_ptr<const SkeletalMeshDeformationData> deformation_;
        std::vector<std::unique_ptr<BoneMatrixBuffer>> bone_buffers_;
    };
} // namespace toy3d
