#pragma once

#include "asset_identity.h"
#include "file_system/physical_path.h"
#include "texture_asset/texture_asset.h"

#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    bool prepare_texture_asset_from_source(const PhysicalPath& source,
        Texture2DAsset& texture, std::string& error);
    bool publish_texture_asset(EditorWorkspace& workspace, const std::string& destination,
        const AssetId& id, const Texture2DAsset& texture, AssetId& published_id, std::string& error);
} // namespace toy3d
