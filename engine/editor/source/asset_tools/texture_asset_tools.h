#pragma once

#include "asset_identity.h"
#include "file_system/physical_path.h"

#include <string>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;
    bool prepare_texture_asset_from_source(const PhysicalPath& source, const AssetId& id,
        std::vector<std::uint8_t>& bytes, std::string& error);
    bool publish_texture_asset(EditorWorkspace& workspace, const std::string& destination,
        const AssetId& id, const std::vector<std::uint8_t>& bytes, AssetId& published_id, std::string& error);
} // namespace toy3d
