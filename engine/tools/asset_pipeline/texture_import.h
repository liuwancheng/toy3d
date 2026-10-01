#pragma once

#include "asset/texture/texture_asset.h"

namespace toy3d
{
    // Pure CPU producer. The Editor owns source selection and CreateNew publication.
    AssetResult<Texture2DAsset> import_texture_image(const std::vector<std::uint8_t>& source);
} // namespace toy3d
