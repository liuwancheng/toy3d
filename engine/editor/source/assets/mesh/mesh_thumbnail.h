#pragma once

#include "assets/thumbnails/thumbnail_source.h"

namespace toy3d
{
    AssetResult<ThumbnailSource> load_mesh_thumbnail_source(const FileSystem& files, AssetPairStore& pairs,
                                                            const AssetCatalogEntry& asset);
} // namespace toy3d
