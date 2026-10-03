#pragma once

#include "asset/thumbnail/asset_thumbnail.h"
#include "assets/animation/animation_preview_asset.h"
#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    // Owned worker input survives loading, GPU capture and conflict validation.
    // World, runtime material and RHI ownership remain with GT/RT.
    struct ThumbnailSource
    {
        AssetThumbnailSource source;
        StaticMeshAssetGeometry geometry;
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> pixels;
        std::shared_ptr<const AnimationPreviewAsset> skeletal;
        std::string warning;
    };

    VirtualPath thumbnail_cache_path(const AssetId& id, const AssetThumbnailSource& source);
    AssetResult<ThumbnailSource> load_thumbnail_source(FileSystem& files, AssetPairStore& pairs,
                                                       const AssetCatalog& catalog, const AssetCatalogEntry& asset,
                                                       bool force);
    // Catalog checks are GT-only and do not read the filesystem.
    bool thumbnail_catalog_current(const AssetCatalog& catalog, const AssetId& id, const std::string& path,
                                   const AnimationPreviewAsset* skeletal);
    // Disk validation and cache publication run only in the owned CPU job.
    AssetStatus validate_thumbnail_source(AssetPairStore& pairs, const FileSystem& files, const AssetCatalog& catalog,
                                          const AssetId& id, const VirtualPath& path,
                                          const std::vector<std::uint8_t>& original,
                                          const AnimationPreviewAsset* skeletal);
    AssetStatus save_thumbnail_cache(FileSystem& files, AssetPairStore& pairs, const AssetCatalog& catalog,
                                     const AssetId& id, const VirtualPath& path, const ThumbnailSource& source,
                                     std::vector<std::uint8_t> pixels);
} // namespace toy3d
