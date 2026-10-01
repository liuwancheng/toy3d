#pragma once

#include "asset/material/material_asset.h"

namespace toy3d
{
    class EditorWorkspace;

    enum class MaterialAssetCreationKind { Material, MaterialInstance };

    bool material_asset_destination(const std::string& folder, const std::string& name,
        std::string& destination, std::string& error);
    AssetStatus create_material_asset_in_workspace(EditorWorkspace& workspace, const std::string& destination,
        const MaterialAssetData& data, const shader::ShaderParameterSchema& schema, AssetId& published_id,
        const std::string& registered_shader_name = "Toy3d/Surface/Phong");
    AssetStatus create_material_instance_asset_in_workspace(EditorWorkspace& workspace, const std::string& destination,
        const MaterialInstanceAssetData& data, const shader::ShaderParameterSchema& schema, AssetId& published_id,
        const std::string& registered_shader_name = "Toy3d/Surface/Phong");
}
