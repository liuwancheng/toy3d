#pragma once

#include "animation/animation_pose.h"
#include "asset/mesh/skeletal_mesh_asset.h"
#include "rendercore/material/material.h"

namespace toy3d
{
    class SkeletalMeshRenderData;

    // Immutable CPU mesh and shared geometry; pose buffers belong to each proxy.
    class SkeletalMesh final
    {
      public:
        static AssetResult<std::shared_ptr<const SkeletalMesh>> create(
            std::shared_ptr<const AnimationBoneLayout> layout, SkeletalMeshAsset asset,
            std::vector<MaterialInterfaceRef> materials);
        SkeletalMesh(SkeletalMesh&&) noexcept = default;
        SkeletalMesh(const SkeletalMesh&) = delete;
        SkeletalMesh& operator=(const SkeletalMesh&) = delete;

        const std::shared_ptr<const AnimationBoneLayout>& bone_layout() const;
        const SkeletalMeshAsset& asset() const;
        const std::vector<MaterialInterfaceRef>& material_slots() const;
        SkeletalMeshRenderData* render_data() const noexcept;

      private:
        SkeletalMesh(std::shared_ptr<const AnimationBoneLayout> layout, SkeletalMeshAsset asset,
                     std::vector<MaterialInterfaceRef> materials);
        std::shared_ptr<const AnimationBoneLayout> bone_layout_;
        SkeletalMeshAsset asset_;
        std::vector<MaterialInterfaceRef> materials_;
        std::shared_ptr<SkeletalMeshRenderData> render_data_;
    };

    using SkeletalMeshRef = std::shared_ptr<const SkeletalMesh>;
} // namespace toy3d
