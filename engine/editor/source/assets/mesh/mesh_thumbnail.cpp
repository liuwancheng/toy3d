#include "assets/mesh/mesh_thumbnail.h"

#include <utility>

namespace toy3d
{
    AssetResult<ThumbnailSource> load_mesh_thumbnail_source(const FileSystem& files, AssetPairStore& pairs,
                                                            const AssetCatalogEntry& asset)
    {
        const auto pair = pairs.read(asset.path);
        if (!pair.succeeded())
        {
            return AssetResult<ThumbnailSource>(pair.status());
        }
        const auto& index = pair.value().description.index;
        if (!(index.asset_id == asset.file.asset_id) || index.root_type != "toy3d.StaticMeshAssetData")
        {
            return AssetResult<ThumbnailSource>(AssetStatus{
                AssetErrorCode::Conflict, {}, {}, {}, {}, "Asset identity/type changed during thumbnail load.", {}});
        }
        const auto source = calculate_static_mesh_thumbnail_source(pair.value());
        if (!source.succeeded())
        {
            return AssetResult<ThumbnailSource>(source.status());
        }
        const auto geometry = read_static_mesh_asset(files, asset.path);
        if (!geometry.succeeded())
        {
            return AssetResult<ThumbnailSource>(geometry.status());
        }
        // The decoder has its own validated pair read; reject a different revision.
        const auto current = pairs.read(asset.path);
        if (!current.succeeded() || current.value().description_bytes != pair.value().description_bytes)
        {
            return AssetResult<ThumbnailSource>(
                AssetStatus{AssetErrorCode::Conflict, {}, {}, {}, {}, "Mesh changed between thumbnail reads.", {}});
        }
        ThumbnailSource result;
        result.source = source.value();
        result.original = pair.value().description_bytes;
        result.geometry = geometry.value();
        return AssetResult<ThumbnailSource>(std::move(result));
    }
} // namespace toy3d
