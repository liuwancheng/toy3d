#pragma once

#include "asset/asset_identity.h"
#include "file_system/physical_path.h"
#include "asset/texture/texture_asset.h"
#include "asset_pipeline/texture_import.h"
#include "asset_pipeline/environment_import.h"

#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    bool prepare_texture_asset_from_source(const PhysicalPath& source, Texture2DAsset& texture, std::string& error,
                                           const TextureImportSettings& settings = {});
    bool prepare_environment_asset_from_source(const PhysicalPath& source, EnvironmentAsset& environment,
                                               std::string& error, EnvironmentImportSettings settings = {});
    bool publish_environment_asset(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
                                   const EnvironmentAsset& environment, AssetId& published_id, std::string& error);
    bool publish_reimported_texture_asset(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
                                          const Texture2DAsset& texture, const std::vector<std::uint8_t>& baseline,
                                          std::string& error);
    bool publish_texture_asset(EditorWorkspace& workspace, const std::string& destination, const AssetId& id,
                               const Texture2DAsset& texture, AssetId& published_id, std::string& error);
} // namespace toy3d
