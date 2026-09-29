#pragma once

#include <string>
#include "math/vector2.h"

namespace toy3d
{
    class EditorWorkspace;
    class EditorSelection;
    class AssetThumbnailPool;

    struct ContentBrowserActions
    {
        bool import_requested = false;
        bool visible = false;
        Vector2 region_min;
        Vector2 region_max;
        bool accepts_drop(const Vector2& position) const
        {
            return visible && position.x >= region_min.x && position.y >= region_min.y &&
                position.x < region_max.x && position.y < region_max.y;
        }
    };

    ContentBrowserActions draw_content_browser(EditorWorkspace& workspace, EditorSelection& selection, std::string& folder,
                              bool& show_engine_content, AssetThumbnailPool& thumbnails, float& tile_size,
                              bool import_enabled = false);
}
