#pragma once

#include "asset/mesh/skeletal_mesh_asset.h"

namespace toy3d
{
    struct BoneInfluence
    {
        std::uint32_t bone_index = 0;
        float weight = 0.0f;
    };

    struct SkeletalMeshBuildInput
    {
        StaticMeshAssetGeometry mesh;
        std::vector<std::vector<BoneInfluence>> influences;
        std::vector<Matrix4> inverse_bind_matrices;
    };

    struct SkeletalMeshBuildOptions
    {
        bool allow_reduce_influences = false;
    };

    struct SkeletalMeshBuildResult
    {
        SkeletalMeshAsset mesh;
        std::uint32_t reduced_vertex_count = 0;
        float maximum_discarded_weight = 0.0f;
    };

    AssetResult<SkeletalMeshBuildResult> build_skeletal_mesh(const SkeletalMeshBuildInput& input,
                                                             const AssetId& skeleton_id,
                                                             const SkeletonAssetData& skeleton,
                                                             const SkeletalMeshBuildOptions& options = {});
} // namespace toy3d
