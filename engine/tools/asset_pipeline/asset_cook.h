#pragma once

#include "asset/asset_catalog.h"

namespace toy3d
{
    // Validated catalog and project reflection are supplied by the host.
    // Output uses existing runtime pairs, retaining only required payload segments.
    // The caller owns a new staging directory and discards it on any failure.
    AssetStatus cook_runtime_assets(const TypeRegistry& types, const FileSystem& files, const AssetCatalog& catalog,
                                    const PhysicalPath& staging);
} // namespace toy3d
