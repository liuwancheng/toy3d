#pragma once

#include "asset/asset_index.h"
#include "rendercore/texture/texture.h"

namespace toy3d
{
    // Builds the runtime cube descriptor for an Environment asset pair. CPU-only so the loader
    // thread can decode while Texture creation stays on the Game Thread (adopt).
    AssetResult<TextureDesc> build_environment_texture_desc(const FileSystem& files, const AssetIndex& index,
                                                            const AssetRef& reference);
    // Same split for Texture2D: the descriptor is CPU-only, so the loader thread can build it
    // while Texture creation stays on the Game Thread (adopt).
    AssetResult<TextureDesc> build_texture2d_desc(const FileSystem& files, const AssetIndex& index,
                                                  const AssetRef& reference);
} // namespace toy3d
