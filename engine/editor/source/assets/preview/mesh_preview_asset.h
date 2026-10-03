#pragma once

#include "animation/animation_sequence.h"
#include "asset/asset_catalog.h"
#include "asset/asset_pair_store.h"
#include "asset/mesh/skeletal_mesh_asset.h"
#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    struct MeshPreviewAsset
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
        std::shared_ptr<const StaticMeshAssetGeometry> static_mesh;
        std::shared_ptr<const AnimationSequence> sequence;
        std::uint32_t sample_rate = 30;
        bool uses_preview_preference = false;
        AssetId preferred_mesh;
        std::vector<Source> sources;
    };

    // The worker sees a catalog copy and immutable file services only. Adoption
    // checks every descriptor (including its meta digest) against the GT catalog.
    AssetResult<MeshPreviewAsset> load_mesh_preview_asset(AssetPairStore& pairs, const AssetCatalog& catalog,
                                                          const AssetId& id, bool override_selection = false,
                                                          const AssetId& mesh = {}, const AssetId& sequence = {},
                                                          std::shared_ptr<const MeshPreviewAsset> reusable = {},
                                                          const FileSystem* editor_settings = nullptr);
    bool mesh_preview_asset_current(AssetPairStore& pairs, const AssetCatalog& catalog, const MeshPreviewAsset& asset,
                                    const FileSystem* editor_settings = nullptr);
    bool animation_asset_uses_skeleton(const AssetFileIndex& asset, const AssetId& skeleton);
    AssetStatus capture_mesh_material_sources(AssetPairStore& pairs, const AssetCatalog& catalog,
                                              const std::vector<AssetRef>& materials,
                                              std::vector<MeshPreviewAsset::Source>& sources);
    // Descriptor-only list filtering; the worker still validates paired payloads before adoption.
    bool animation_asset_matches_layout(const TypeRegistry& types, const FileSystem& files,
                                        const AssetCatalogEntry& asset, const AnimationBoneLayout& layout);
    AssetResult<AssetId> animation_preview_mesh_preference(const FileSystem& files, const AssetId& animation);
    AssetStatus set_animation_preview_mesh_preference(FileSystem& files, const AssetId& animation, const AssetId& mesh);
} // namespace toy3d
