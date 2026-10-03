#pragma once

#include "asset_pipeline/skeletal_mesh_builder.h"
#include "asset_pipeline/static_mesh_import.h"

namespace toy3d
{
    struct SkeletalMeshImportOptions
    {
        StaticMeshImportOptions coordinates;
        SkeletalMeshBuildOptions skin;
        std::uint32_t sample_rate = 30;
    };

    struct ImportedAnimationSequence
    {
        std::string name;
        AnimationSequenceAsset sequence;
    };

    struct ImportedSkeletalMesh
    {
        SkeletonAssetData skeleton;
        SkeletalMeshAsset mesh;
        std::vector<ImportedAnimationSequence> animations;
        std::vector<std::string> warnings;
        std::vector<ImportedModelSource> sources;
    };

    // Produces a complete owned candidate; publication is a separate caller transaction.
    AssetResult<ImportedSkeletalMesh> import_skeletal_mesh(const FileSystem& files, const VirtualPath& source,
                                                           const AssetId& skeleton_id,
                                                           const SkeletalMeshImportOptions& options = {});

    // Strictly validates source names/parents/reference pose, then remaps all indices to the existing layout.
    // The supplied skeleton is borrowed only during this call; the result owns its canonical copy.
    AssetResult<ImportedSkeletalMesh> import_skeletal_mesh(const FileSystem& files, const VirtualPath& source,
                                                           const AssetId& skeleton_id,
                                                           const SkeletonAssetData& existing_skeleton,
                                                           const SkeletalMeshImportOptions& options = {});
} // namespace toy3d
