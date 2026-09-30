#include "rendercore/texture/texture_asset_loader.h"

#include "texture_asset/texture_asset.h"

namespace toy3d
{
    AssetResult<TextureRef> load_texture_asset(const FileSystem& files, const AssetIndex& index,
        const AssetRef& reference)
    {
        const AssetStatus resolved = index.resolve(reference, "texture");
        if (!resolved.succeeded()) return AssetResult<TextureRef>(resolved);
        if (reference.expected_type != "toy3d.Texture2DAssetData" || reference.subresource_id.valid() ||
            reference.strength != AssetRefStrength::Strong)
            return AssetResult<TextureRef>({AssetErrorCode::TypeMismatch, reference.asset_id, {}, {}, "texture",
                "Material texture must reference a strong Texture2D root.", {}});
        const AssetLocation* location = index.find(reference.asset_id);
        if (!location) return AssetResult<TextureRef>({AssetErrorCode::MissingReference, reference.asset_id, {}, {},
            "texture", "Texture2D asset was not found.", {}});
        const auto loaded = read_texture_asset(files, location->path);
        if (!loaded.succeeded()) return AssetResult<TextureRef>(loaded.status());
        TextureDesc desc;
        desc.width = loaded.value().width;
        desc.height = loaded.value().height;
        desc.format = loaded.value().format;
        for (const auto& mip : loaded.value().mips)
        {
            desc.row_pitches.push_back(mip.row_pitch);
            desc.slice_pitches.push_back(mip.slice_pitch);
            desc.mip_pixels.push_back(mip.pixels);
        }
        TextureRef texture = Texture::create(std::move(desc));
        if (!texture) return AssetResult<TextureRef>({AssetErrorCode::Value, reference.asset_id,
            location->path.utf8(), "texture_mips", {}, "Texture2D runtime descriptor is invalid.", {}});
        return AssetResult<TextureRef>(std::move(texture));
    }
} // namespace toy3d
