#pragma once

#include "animation/animation_sequence.h"
#include "asset/asset_catalog.h"
#include "asset/asset_pair_store.h"
#include "asset/mesh/skeletal_mesh_asset.h"

namespace toy3d
{
    struct AnimationPreviewAsset
    {
        struct Source
        {
            AssetId id;
            VirtualPath path;
            std::vector<std::uint8_t> description;
        };
        AssetId id;
        AssetId mesh_id;
        AssetId sequence_id;
        std::string path;
        std::string root_type;
        std::shared_ptr<const AnimationBoneLayout> layout;
        std::shared_ptr<const SkeletalMeshAsset> mesh;
        std::shared_ptr<const AnimationSequence> sequence;
        std::uint32_t sample_rate = 30;
        std::vector<Source> sources;
    };

    // The worker sees a catalog copy and immutable file services only. Adoption
    // checks every descriptor (including its meta digest) against the GT catalog.
    AssetResult<AnimationPreviewAsset> load_animation_preview_asset(AssetPairStore& pairs, const AssetCatalog& catalog,
                                                                    const AssetId& id, bool override_selection = false,
                                                                    const AssetId& mesh = {},
                                                                    const AssetId& sequence = {});
    bool animation_preview_asset_current(AssetPairStore& pairs, const AssetCatalog& catalog,
                                         const AnimationPreviewAsset& asset);
    bool animation_asset_uses_skeleton(const AssetFileIndex& asset, const AssetId& skeleton);
} // namespace toy3d
