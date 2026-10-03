#pragma once

#include "asset/texture/texture_asset.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    enum class TexturePreviewChannel
    {
        RGBA,
        Red,
        Green,
        Blue,
        Alpha
    };

    // Converts a selected authored mip into an opaque or alpha-preserving BGRA
    // UI image without changing the stored Texture2D asset.
    bool make_texture_preview_pixels(const Texture2DAsset& asset, std::uint32_t mip, TexturePreviewChannel channel,
                                     std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& bgra,
                                     std::string& error);
    // Select a small authored mip and fit it into the fixed opaque thumbnail.
    bool make_texture_thumbnail_pixels(const Texture2DAsset& asset, std::vector<std::uint8_t>& bgra,
                                       std::string& error);
} // namespace toy3d
