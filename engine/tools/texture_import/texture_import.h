#pragma once

#include "texture_asset/texture_asset.h"

namespace toy3d
{
    // Pure CPU producer. The Editor owns source selection and CreateNew publication.
    AssetResult<Texture2DAsset> import_texture_image(const std::vector<std::uint8_t>& source);
    AssetResult<std::vector<std::uint8_t>> import_texture_asset(
        const std::vector<std::uint8_t>& source, const AssetId& id);
} // namespace toy3d
