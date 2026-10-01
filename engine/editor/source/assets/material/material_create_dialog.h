#pragma once

#include "assets/material/material_asset_tools.h"

#include <array>

namespace toy3d
{
    class EditorSelection;
    class EditorWorkspace;
    class MaterialShaderWorkflow;

    class MaterialCreateDialog final
    {
      public:
        void request(MaterialAssetCreationKind kind, const std::string& folder, AssetId parent = {});
        void draw(EditorWorkspace& workspace, EditorSelection& selection, std::string& browser_folder,
            const shader::ShaderParameterSchema& schema, MaterialShaderWorkflow* shaders = nullptr);
        bool active() const { return active_; }
        void clear();

      private:
        MaterialAssetCreationKind kind_ = MaterialAssetCreationKind::Material;
        std::array<char, 256> name_{};
        std::string folder_;
        AssetId parent_;
        AssetId published_id_;
        std::string error_;
        std::string shader_name_ = "Toy3d/Surface/Phong";
        bool two_sided_ = false;
        bool active_ = false;
        bool open_ = false;
    };
}
