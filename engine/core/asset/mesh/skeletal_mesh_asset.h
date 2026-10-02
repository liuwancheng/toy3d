#pragma once

#include "asset/animation/animation_asset.h"
#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    // C++17 inline constants keep the section ABI shared by builder and runtime.
    inline constexpr std::size_t max_section_bones = 256;
    inline constexpr std::size_t skin_influences_per_group = 4;
    inline constexpr std::size_t max_skin_influences = 8;
    inline constexpr float skin_bind_tolerance = 0.001f;

    struct SkinWeights
    {
        std::array<std::uint8_t, max_skin_influences> bone_indices{};
        std::array<std::uint8_t, max_skin_influences> weights{};
    };

    struct BoneLocalBounds
    {
        bool influenced = false;
        Vector3 minimum;
        Vector3 maximum;
    };

    struct SkeletalMeshAssetGeometry
    {
        // Reuse the static vertex attributes and triangle validation, not its asset identity.
        StaticMeshAssetGeometry mesh;
        std::uint32_t num_bone_influences = skin_influences_per_group;
        std::vector<SkinWeights> skin_weights;
        std::vector<std::vector<std::uint32_t>> section_bone_maps;
        std::vector<Matrix4> inverse_bind_matrices;
        std::vector<BoneLocalBounds> bone_local_bounds;
    };

    struct SkeletalMeshAsset
    {
        SkeletalMeshAssetData data;
        SkeletalMeshAssetGeometry geometry;
    };

    AssetStatus validate_skeletal_mesh_geometry(const SkeletalMeshAssetGeometry& geometry);
    AssetStatus validate_skeletal_mesh(const SkeletalMeshAsset& mesh);
    AssetStatus validate_skeletal_mesh_compatibility(const SkeletalMeshAsset& mesh, const AssetId& skeleton_id,
                                                     const SkeletonAssetData& skeleton);
    AssetResult<std::vector<std::uint8_t>> encode_skeletal_mesh_geometry(const SkeletalMeshAssetGeometry& geometry);
    AssetResult<SkeletalMeshAssetGeometry> decode_skeletal_mesh_geometry(const std::vector<std::uint8_t>& bytes);
    AssetResult<AssetPairBytes> encode_skeletal_mesh_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                                const SkeletalMeshAsset& mesh);
    AssetResult<SkeletalMeshAsset> decode_skeletal_mesh_asset_pair(const AssetPair& pair);
} // namespace toy3d
