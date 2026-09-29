#pragma once

#include "asset_index.h"
#include "format/shader_format_types.h"
#include "material/material_asset_data.h"
#include "material_asset_reflection.h"

#include <cstddef>

namespace toy3d
{
    constexpr std::size_t maximum_material_overrides = 4096u;

    AssetStatus validate_material_asset(const MaterialAssetData& data, const AssetIndex* index = nullptr);
    AssetStatus validate_material_instance_asset(const MaterialInstanceAssetData& data, const AssetIndex* index = nullptr);
    bool material_override_matches_schema(const MaterialParameterOverride& value,
        const shader::ShaderParameterSchema& schema);
    AssetStatus validate_material_overrides_schema(const std::vector<MaterialParameterOverride>& values,
        const shader::ShaderParameterSchema& schema);
    std::vector<AssetRef> material_asset_dependencies(const MaterialAssetData& data);
    std::vector<AssetRef> material_asset_dependencies(const MaterialInstanceAssetData& data);

    // Creation uses a canonical sorted copy; existing edits use EditSession/save_asset.
    AssetResult<std::vector<std::uint8_t>> encode_material_asset(const TypeRegistry& types,
        const AssetId& id, const MaterialAssetData& data, const AssetIndex* index = nullptr);
    AssetResult<std::vector<std::uint8_t>> encode_material_instance_asset(const TypeRegistry& types,
        const AssetId& id, const MaterialInstanceAssetData& data, const AssetIndex* index = nullptr);
    AssetStatus read_material_asset(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& path, MaterialAssetData& output, const AssetIndex* index = nullptr);
    AssetStatus read_material_instance_asset(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& path, MaterialInstanceAssetData& output, const AssetIndex* index = nullptr);
}
