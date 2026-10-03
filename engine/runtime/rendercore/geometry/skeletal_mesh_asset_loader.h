#pragma once

#include "asset/asset_index.h"
#include "asset/scene/scene_asset_data.h"
#include "animation/animation_sequence.h"
#include "rendercore/geometry/skeletal_mesh.h"

namespace toy3d
{
    // Validated immutable CPU assets. Components own playback and render resources separately.
    struct SkeletalMeshAssets
    {
        SkeletalMeshRef mesh;
        std::shared_ptr<const AnimationSequence> sequence;
    };

    AssetResult<SkeletalMeshAssets> load_skeletal_mesh_assets(const TypeRegistry& types, const FileSystem& files,
                                                              const AssetIndex& index,
                                                              const SceneSkeletalMeshData& data,
                                                              const MaterialInterfaceRef& default_material);
} // namespace toy3d
