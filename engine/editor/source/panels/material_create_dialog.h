#pragma once

#include "asset_tools/material_asset_tools.h"

#include <array>

namespace toy3d
{
    class EditorSelection;
    class EditorWorkspace;

    class MaterialCreateDialog final
    {
      public:
        void request(MaterialAssetCreationKind kind, const std::string& folder, AssetId parent = {});
        void draw(EditorWorkspace& workspace, EditorSelection& selection, std::string& browser_folder,
            const shader::ShaderParameterSchema& schema);
        bool active() const { return active_; }
        void clear();

      private:
        MaterialAssetCreationKind kind_ = MaterialAssetCreationKind::Material;
        std::array<char, 256> name_{};
        std::string folder_;
        AssetId parent_;
        AssetId published_id_;
        std::string error_;
        bool two_sided_ = false;
        bool active_ = false;
        bool open_ = false;
    };
}
