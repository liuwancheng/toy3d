#pragma once

#include "asset/material/material_asset.h"
#include "rendercore/material/material.h"

#include <map>

namespace toy3d
{
    // Strong resolved textures belong to the candidate owner, not a global cache.
    struct MaterialTextureValues
    {
        std::map<std::string, TextureRef> named_defaults;
        std::map<AssetId, TextureRef> assets;
    };

    AssetResult<MaterialDesc> material_descriptor_from_asset(const MaterialAssetData& data,
                                                             std::shared_ptr<const ShaderMapProgram> program,
                                                             const MaterialTextureValues& textures);
    AssetResult<MaterialParameterChanges> material_changes_from_overrides(
        const std::vector<MaterialParameterOverride>& overrides, const shader::ShaderParameterSchema& schema,
        const MaterialTextureValues& textures);

    AssetResult<MaterialInstanceRef> create_material_from_asset(const MaterialAssetData& data,
                                                                std::shared_ptr<const ShaderMapProgram> program,
                                                                const MaterialTextureValues& textures);
    AssetResult<MaterialInstanceRef> create_material_instance_from_asset(const MaterialInstanceAssetData& data,
                                                                         MaterialInterfaceRef parent,
                                                                         const MaterialTextureValues& textures);
} // namespace toy3d
