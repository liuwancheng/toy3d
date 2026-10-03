#pragma once

#include "assets/thumbnails/thumbnail_source.h"

namespace toy3d
{
    AssetResult<ThumbnailSource> load_animation_thumbnail_source(AssetPairStore& pairs, const FileSystem& files,
                                                                 const AssetCatalog& catalog,
                                                                 const AssetCatalogEntry& asset);
} // namespace toy3d
