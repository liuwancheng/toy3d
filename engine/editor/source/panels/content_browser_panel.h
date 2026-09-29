#pragma once

#include <string>

namespace toy3d
{
    class EditorWorkspace;
    class EditorSelection;
    class AssetThumbnailPool;

    void draw_content_browser(EditorWorkspace& workspace, EditorSelection& selection, std::string& folder,
                              bool& show_engine_content, AssetThumbnailPool& thumbnails, float& tile_size);
}
