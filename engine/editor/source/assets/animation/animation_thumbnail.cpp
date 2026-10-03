#include "assets/animation/animation_thumbnail.h"

#include <utility>

namespace toy3d
{
    namespace
    {
        AssetStatus conflict(const std::string& message)
        {
            return {AssetErrorCode::Conflict, {}, {}, {}, {}, message, {}};
        }
    } // namespace

    AssetResult<ThumbnailSource> load_animation_thumbnail_source(AssetPairStore& pairs, const FileSystem& files,
                                                                 const AssetCatalog& catalog,
                                                                 const AssetCatalogEntry& asset)
    {
        const auto pair = pairs.read(asset.path);
        if (!pair.succeeded())
        {
            return AssetResult<ThumbnailSource>(pair.status());
        }
        const auto& index = pair.value().description.index;
        if (!(index.asset_id == asset.file.asset_id) || index.root_type != asset.file.root_type)
        {
            return AssetResult<ThumbnailSource>(conflict("Asset identity/type changed during thumbnail load."));
        }
        auto loaded = load_mesh_preview_asset(pairs, catalog, asset.file.asset_id, false, {}, {}, {}, &files);
        if (!loaded.succeeded())
        {
            return AssetResult<ThumbnailSource>(loaded.status());
        }
        auto skeletal = std::make_shared<const MeshPreviewAsset>(std::move(loaded).value());
        if (!skeletal->mesh)
        {
            return AssetResult<ThumbnailSource>(conflict(
                "Animation thumbnail requires a compatible preview mesh. Select one in the Animation editor."));
        }
        const auto* skeleton_location = catalog.index.find(skeletal->layout->skeleton_id());
        if (!skeleton_location)
        {
            return AssetResult<ThumbnailSource>(conflict("Thumbnail Skeleton was removed."));
        }
        const auto skeleton_pair = pairs.read(skeleton_location->path);
        if (!skeleton_pair.succeeded())
        {
            return AssetResult<ThumbnailSource>(skeleton_pair.status());
        }
        for (const auto& input : skeletal->sources)
        {
            if ((input.id == asset.file.asset_id && input.description != pair.value().description_bytes) ||
                (input.id == skeletal->layout->skeleton_id() &&
                 input.description != skeleton_pair.value().description_bytes))
            {
                return AssetResult<ThumbnailSource>(conflict("Skeletal thumbnail inputs changed between reads."));
            }
        }
        AssetResult<AssetThumbnailSource> source =
            calculate_skeletal_mesh_thumbnail_source(pair.value(), skeleton_pair.value());
        if (asset.file.root_type == "toy3d.AnimationSequenceAssetData")
        {
            const auto* mesh_location = catalog.index.find(skeletal->mesh_id);
            if (!mesh_location)
            {
                return AssetResult<ThumbnailSource>(conflict("Thumbnail preview mesh was removed."));
            }
            const auto mesh_pair = pairs.read(mesh_location->path);
            if (!mesh_pair.succeeded())
            {
                return AssetResult<ThumbnailSource>(mesh_pair.status());
            }
            for (const auto& input : skeletal->sources)
            {
                if (input.id == skeletal->mesh_id && input.description != mesh_pair.value().description_bytes)
                {
                    return AssetResult<ThumbnailSource>(conflict("Animation preview mesh changed between reads."));
                }
            }
            source = calculate_animation_thumbnail_source(pair.value(), mesh_pair.value(), skeleton_pair.value());
        }
        if (!source.succeeded())
        {
            return AssetResult<ThumbnailSource>(source.status());
        }
        ThumbnailSource result;
        result.source = source.value();
        result.original = pair.value().description_bytes;
        result.skeletal = std::move(skeletal);
        return AssetResult<ThumbnailSource>(std::move(result));
    }
} // namespace toy3d
