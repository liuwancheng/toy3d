#pragma once

#include "asset_index.h"
#include "rendercore/texture/texture.h"

namespace toy3d
{
    AssetResult<TextureRef> load_texture_asset(const FileSystem& files, const AssetIndex& index,
        const AssetRef& reference);
} // namespace toy3d
