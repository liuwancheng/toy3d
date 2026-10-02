#include "rendercore/geometry/skeletal_mesh.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    AssetResult<SkeletalMeshRef> SkeletalMesh::create(std::shared_ptr<const AnimationBoneLayout> layout,
                                                      SkeletalMeshAsset asset,
                                                      std::vector<MaterialInterfaceRef> materials)
    {
        if (!layout || !layout->status().succeeded())
        {
            return AssetResult<SkeletalMeshRef>(
                AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, "Invalid bone layout.", {}});
        }
        const auto status = validate_skeletal_mesh_compatibility(asset, layout->skeleton_id(), layout->skeleton());
        if (!status.succeeded())
        {
            return AssetResult<SkeletalMeshRef>(AssetStatus{status.code, {}, {}, {}, {}, status.message, {}});
        }
        if (materials.size() != asset.data.material_slots.size() || std::any_of(materials.begin(), materials.end(),
                                                                                [](const MaterialInterfaceRef& material)
                                                                                {
                                                                                    return !material;
                                                                                }))
        {
            return AssetResult<SkeletalMeshRef>(
                AssetStatus{AssetErrorCode::Value, {}, {}, {}, {}, "Mesh requires every material slot.", {}});
        }
        SkeletalMesh candidate(std::move(layout), std::move(asset), std::move(materials));
        return AssetResult<SkeletalMeshRef>(std::make_shared<SkeletalMesh>(std::move(candidate)));
    }

    SkeletalMesh::SkeletalMesh(std::shared_ptr<const AnimationBoneLayout> layout, SkeletalMeshAsset asset,
                               std::vector<MaterialInterfaceRef> materials)
        : bone_layout_(std::move(layout)), asset_(std::move(asset)), materials_(std::move(materials))
    {
    }

    const std::shared_ptr<const AnimationBoneLayout>& SkeletalMesh::bone_layout() const
    {
        return bone_layout_;
    }

    const SkeletalMeshAsset& SkeletalMesh::asset() const
    {
        return asset_;
    }

    const std::vector<MaterialInterfaceRef>& SkeletalMesh::material_slots() const
    {
        return materials_;
    }
} // namespace toy3d
