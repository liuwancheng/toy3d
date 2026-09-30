#pragma once

#include "asset_index.h"
#include "format/shader_format_types.h"
#include "material/material_asset_data.h"
#include "material_asset_reflection.h"

#include <cstddef>

namespace toy3d
{
    constexpr std::size_t maximum_material_overrides = 4096u;
    constexpr std::size_t maximum_material_parent_depth = 64u;

    bool is_material_asset_type(const std::string& type);
    bool parse_material_sampler_preset(const std::string& name, MaterialSamplerPreset& output);

    // Ordered root -> leaf. Only each layer's own values are kept; the resolved
    // view is transient and never replaces the persisted Parent or overrides.
    struct MaterialAssetLayer
    {
        AssetRef reference;
        std::vector<MaterialParameterOverride> overrides;
    };
    struct MaterialAssetHierarchy
    {
        MaterialAssetData root;
        std::vector<MaterialAssetLayer> layers;
        std::vector<MaterialParameterOverride> effective_overrides(const shader::ShaderParameterSchema& schema) const;
    };
    AssetResult<MaterialAssetHierarchy> read_material_hierarchy(const TypeRegistry& types,
        const FileSystem& files, const AssetIndex& index, const AssetRef& leaf);

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
