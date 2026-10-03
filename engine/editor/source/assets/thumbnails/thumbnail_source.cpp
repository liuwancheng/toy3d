#include "assets/thumbnails/thumbnail_source.h"

#include <utility>

#include "assets/animation/animation_thumbnail.h"
#include "assets/mesh/mesh_thumbnail.h"
#include "assets/texture/texture_preview_image.h"
#include "image/png_codec.h"

namespace toy3d
{
    namespace
    {
        AssetStatus failure(const std::string& message, AssetErrorCode code = AssetErrorCode::Conflict)
        {
            return {code, {}, {}, {}, {}, message, {}};
        }
    } // namespace

    VirtualPath thumbnail_cache_path(const AssetId& id, const AssetThumbnailSource& source)
    {
        return VirtualPath::parse("/Saved/AssetThumbnails/" + id.hex() + "-" + sha256_to_hex(source.content_hash) +
                                  "-v" + std::to_string(thumbnail_generator_version) + ".png")
            .value();
    }

    bool thumbnail_catalog_current(const AssetCatalog& catalog, const AssetId& id, const std::string& path,
                                   const AnimationPreviewAsset* skeletal)
    {
        const auto* location = catalog.index.find(id);
        if (!location || location->path.utf8() != path)
        {
            return false;
        }
        if (skeletal)
        {
            for (const auto& source : skeletal->sources)
            {
                const auto* dependency = catalog.index.find(source.id);
                if (!dependency || dependency->path.utf8() != source.path.utf8())
                {
                    return false;
                }
            }
        }
        return true;
    }

    AssetStatus validate_thumbnail_source(AssetPairStore& pairs, const FileSystem& files, const AssetCatalog& catalog,
                                          const AssetId& id, const VirtualPath& path,
                                          const std::vector<std::uint8_t>& original,
                                          const AnimationPreviewAsset* skeletal)
    {
        if (!thumbnail_catalog_current(catalog, id, path.utf8(), skeletal))
        {
            return failure("Thumbnail conflict: asset or dependency was moved or removed.");
        }
        const auto current = pairs.read(path);
        if (!current.succeeded())
        {
            return current.status();
        }
        if (!(current.value().description.index.asset_id == id) ||
            sha256(current.value().description_bytes) != sha256(original))
        {
            return failure("Thumbnail conflict: asset changed; refresh and regenerate.");
        }
        if (skeletal && !animation_preview_asset_current(pairs, catalog, *skeletal, &files))
        {
            return failure("Thumbnail conflict: skeletal inputs changed; refresh and regenerate.");
        }
        return AssetStatus::success();
    }

    AssetResult<ThumbnailSource> load_thumbnail_source(FileSystem& files, AssetPairStore& pairs,
                                                       const AssetCatalog& catalog, const AssetCatalogEntry& asset,
                                                       bool force)
    {
        if (asset.file.root_type == "toy3d.Texture2DAssetData")
        {
            const auto pair = pairs.read(asset.path);
            if (!pair.succeeded())
            {
                return AssetResult<ThumbnailSource>(pair.status());
            }
            if (!(pair.value().description.index.asset_id == asset.file.asset_id) ||
                pair.value().description.index.root_type != asset.file.root_type)
            {
                return AssetResult<ThumbnailSource>(failure("Texture thumbnail identity/type changed."));
            }
            const auto loaded = read_texture_asset(files, asset.path);
            if (!loaded.succeeded())
            {
                return AssetResult<ThumbnailSource>(loaded.status());
            }
            ThumbnailSource result;
            result.original = pair.value().description_bytes;
            std::string error;
            if (!make_texture_thumbnail_pixels(loaded.value(), result.pixels, error))
            {
                return AssetResult<ThumbnailSource>(failure(error, AssetErrorCode::Value));
            }
            const auto current = validate_thumbnail_source(pairs, files, catalog, asset.file.asset_id, asset.path,
                                                           result.original, nullptr);
            if (!current.succeeded())
            {
                return AssetResult<ThumbnailSource>(current);
            }
            return AssetResult<ThumbnailSource>(std::move(result));
        }
        const bool skeletal = asset.file.root_type == "toy3d.SkeletalMeshAssetData" ||
                              asset.file.root_type == "toy3d.AnimationSequenceAssetData";
        ThumbnailSource result;
        if (skeletal)
        {
            auto loaded = load_animation_thumbnail_source(pairs, files, catalog, asset);
            if (!loaded.succeeded())
            {
                return loaded;
            }
            result = std::move(loaded).value();
        }
        else if (asset.file.root_type == "toy3d.StaticMeshAssetData")
        {
            const auto pair = pairs.read(asset.path);
            if (!pair.succeeded())
            {
                return AssetResult<ThumbnailSource>(pair.status());
            }
            if (!(pair.value().description.index.asset_id == asset.file.asset_id))
            {
                return AssetResult<ThumbnailSource>(failure("Thumbnail asset identity changed."));
            }
            const auto source = calculate_static_mesh_thumbnail_source(pair.value());
            if (!source.succeeded())
            {
                return AssetResult<ThumbnailSource>(source.status());
            }
            result.source = source.value();
            result.original = pair.value().description_bytes;
        }
        else
        {
            return AssetResult<ThumbnailSource>(
                failure("Unsupported thumbnail asset type.", AssetErrorCode::TypeMismatch));
        }
        if (!force)
        {
            const auto bytes =
                files.read_binary(thumbnail_cache_path(asset.file.asset_id, result.source), thumbnail_max_bytes);
            if (bytes.succeeded())
            {
                Rgba8Image decoded;
                if (decode_png(bytes.value(), decoded).succeeded() && decoded.width == thumbnail_default_size &&
                    decoded.height == thumbnail_default_size)
                {
                    result.pixels = std::move(decoded.pixels);
                    for (std::size_t i = 0; i < result.pixels.size(); i += 4)
                    {
                        std::swap(result.pixels[i], result.pixels[i + 2]);
                        result.pixels[i + 3] = 255;
                    }
                }
                else
                {
                    result.warning = "Cached thumbnail is invalid or stale; generating a replacement.";
                }
            }
            else if (bytes.status().code != FileErrorCode::NotFound)
            {
                result.warning =
                    "Cached thumbnail could not be read; generating a replacement: " + bytes.status().message;
            }
        }
        if (!skeletal && result.pixels.empty())
        {
            auto loaded = load_mesh_thumbnail_source(files, pairs, asset);
            if (!loaded.succeeded())
            {
                return loaded;
            }
            if (loaded.value().original != result.original)
            {
                return AssetResult<ThumbnailSource>(failure("Mesh changed during thumbnail preparation."));
            }
            result.geometry = std::move(loaded).value().geometry;
        }
        const auto current = validate_thumbnail_source(pairs, files, catalog, asset.file.asset_id, asset.path,
                                                       result.original, result.skeletal.get());
        if (!current.succeeded())
        {
            return AssetResult<ThumbnailSource>(current);
        }
        return AssetResult<ThumbnailSource>(std::move(result));
    }

    AssetStatus save_thumbnail_cache(FileSystem& files, AssetPairStore& pairs, const AssetCatalog& catalog,
                                     const AssetId& id, const VirtualPath& path, const ThumbnailSource& source,
                                     std::vector<std::uint8_t> pixels)
    {
        if (pixels.size() != static_cast<std::size_t>(thumbnail_default_size) * thumbnail_default_size * 4u)
        {
            return failure("Thumbnail GPU readback dimensions are invalid.", AssetErrorCode::Value);
        }
        Rgba8Image image;
        image.width = image.height = thumbnail_default_size;
        image.pixels = std::move(pixels);
        for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        {
            std::swap(image.pixels[i], image.pixels[i + 2]);
            image.pixels[i + 3] = 255;
        }
        std::vector<std::uint8_t> png;
        const auto encoded = encode_png(image, png);
        if (!encoded.succeeded())
        {
            return failure(encoded.message, AssetErrorCode::Value);
        }
        const auto current =
            validate_thumbnail_source(pairs, files, catalog, id, path, source.original, source.skeletal.get());
        if (!current.succeeded())
        {
            return current;
        }
        const auto directory = VirtualPath::parse("/Saved/AssetThumbnails");
        const auto made = files.create_directories(directory.value());
        if (!made.succeeded())
        {
            return {AssetErrorCode::Io,
                    id,
                    directory.value().utf8(),
                    {},
                    {},
                    "Could not create thumbnail cache directory: " + made.message,
                    made};
        }
        const auto saved =
            files.write_binary_atomic(thumbnail_cache_path(id, source.source), png, FilePublishMode::Replace);
        if (!saved.succeeded())
        {
            return {AssetErrorCode::Io,
                    id,
                    thumbnail_cache_path(id, source.source).utf8(),
                    {},
                    {},
                    "Could not save thumbnail cache: " + saved.message,
                    saved};
        }
        return AssetStatus::success();
    }
} // namespace toy3d
