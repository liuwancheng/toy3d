#pragma once

#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "imgui.h"

namespace toy3d
{
    void draw_asset_placeholder(ImDrawList& draw, ImVec2 position, float size, bool folder, const std::string& type);
    void paint_asset_thumbnail(ImDrawList& draw, ImVec2 position, float size, const AssetThumbnailView& view,
                               const std::string& type);
    bool asset_thumbnail_widget(const AssetThumbnailView& view, const std::string& type, float size,
                                bool interactive = false);
} // namespace toy3d
