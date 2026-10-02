#pragma once

#include "animation/animation_pose.h"
#include "asset/mesh/skeletal_mesh_asset.h"
#include "rendercore/material/material.h"

namespace toy3d
{
    // Immutable CPU mesh shared by GT consumers. Each scene proxy owns its render data.
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

      private:
        SkeletalMesh(std::shared_ptr<const AnimationBoneLayout> layout, SkeletalMeshAsset asset,
                     std::vector<MaterialInterfaceRef> materials);
        std::shared_ptr<const AnimationBoneLayout> bone_layout_;
        SkeletalMeshAsset asset_;
        std::vector<MaterialInterfaceRef> materials_;
    };

    using SkeletalMeshRef = std::shared_ptr<const SkeletalMesh>;
} // namespace toy3d
