#pragma once

#include "asset_pair.h"
#include "asset_index.h"
#include "scene_asset/scene_asset_data.h"
#include "scene_asset_reflection.h"

namespace toy3d
{
    const char* scene_root_component_type(const std::string& kind);
    AssetStatus validate_scene_asset(const SceneAssetData& data, const AssetIndex* index = nullptr);
    AssetResult<AssetPairBytes> encode_scene_asset_pair(const TypeRegistry& types,
        const AssetId& id, const SceneAssetData& data, const AssetIndex* index = nullptr);
    AssetStatus read_scene_asset(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& path, SceneAssetData& output, const AssetIndex* index = nullptr);
}
