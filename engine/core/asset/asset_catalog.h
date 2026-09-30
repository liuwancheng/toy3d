#pragma once

#include "asset_index.h"

#include <vector>

namespace toy3d
{
    struct AssetCatalogEntry
    {
        VirtualPath path;
        AssetFileIndex file;
    };

    struct AssetCatalog
    {
        AssetIndex index;
        std::vector<VirtualPath> directories;
        std::vector<AssetCatalogEntry> entries;
    };

    // A failed scan never publishes a partial index or list.
    // Scan all roots before validating references, so project assets may depend
    // on engine assets. Roots must be nonempty and must not overlap.
    AssetResult<AssetCatalog> scan_asset_catalog(const TypeRegistry& types,
        const FileSystem& files, const std::vector<VirtualPath>& roots);
} // namespace toy3d
