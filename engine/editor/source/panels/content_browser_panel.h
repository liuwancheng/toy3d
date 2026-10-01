#pragma once

#include <string>
#include "math/vector2.h"
#include "assets/material/material_asset_tools.h"

namespace toy3d
{
    class EditorWorkspace;
    class EditorSelection;
    class AssetThumbnailPool;

    struct ContentBrowserActions
    {
        bool import_requested = false;
        bool texture_import_requested = false;
        bool material_creation_requested = false;
        MaterialAssetCreationKind material_creation_kind = MaterialAssetCreationKind::Material;
        AssetId material_parent;
        AssetId asset_open;
        bool asset_focus = false;
        bool assets_refreshed = false;
        bool visible = false;
        Vector2 region_min;
        Vector2 region_max;
        bool accepts_drop(const Vector2& position) const
        {
            return visible && position.x >= region_min.x && position.y >= region_min.y &&
                position.x < region_max.x && position.y < region_max.y;
        }
    };

    class ContentBrowserPanel final
    {
      public:
        ContentBrowserActions draw(EditorWorkspace& workspace, EditorSelection& selection, std::string& folder,
                                  bool& show_engine_content, AssetThumbnailPool& thumbnails, bool import_enabled = false);
        void clear();

      private:
        AssetId pending_delete_;
        std::string delete_error_;
    };
}
