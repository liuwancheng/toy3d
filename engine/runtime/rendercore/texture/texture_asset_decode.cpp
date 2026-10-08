#include "rendercore/texture/texture_asset_decode.h"

#include <algorithm>

#include "asset/texture/texture_asset.h"
#include "asset/texture/environment_asset.h"

namespace toy3d
{
    AssetResult<TextureDesc> build_environment_texture_desc(const FileSystem& files, const AssetIndex& index,
                                                            const AssetRef& reference)
    {
        const auto resolved = index.resolve(reference, "environment");
        if (!resolved.succeeded())
        {
            return AssetResult<TextureDesc>(resolved);
        }
        if (reference.expected_type != "toy3d.EnvironmentAssetData" || reference.subresource_id.valid() ||
            reference.strength != AssetRefStrength::Strong)
        {
            return AssetResult<TextureDesc>({AssetErrorCode::TypeMismatch,
                                             reference.asset_id,
                                             {},
                                             {},
                                             "environment",
                                             "Environment must reference a strong Environment root.",
                                             {}});
        }
        const AssetLocation* location = index.find(reference.asset_id);
        if (!location)
        {
            return AssetResult<TextureDesc>({AssetErrorCode::MissingReference,
                                             reference.asset_id,
                                             {},
                                             {},
                                             "environment",
                                             "Environment asset was not found.",
                                             {}});
        }
        const auto loaded = read_environment_asset(files, location->path);
        if (!loaded.succeeded())
        {
            return AssetResult<TextureDesc>(loaded.status());
        }
        TextureDesc desc;
        desc.usage = TextureUsage::LinearData;
        desc.width = desc.height = loaded.value().face_size;
        desc.format = PixelFormat::R16G16B16A16Float;
        desc.cube = true;
        desc.requires_linear_filter = true;
        for (std::size_t mip = 0; mip < loaded.value().mips.size(); ++mip)
        {
            const auto size = std::max(1u, desc.width >> static_cast<std::uint32_t>(mip));
            desc.row_pitches.push_back(size * 8u);
            desc.slice_pitches.push_back(static_cast<std::size_t>(size) * size * 8u);
            const std::size_t face_bytes = desc.slice_pitches.back();
            for (const auto& face : loaded.value().mips[mip].faces)
            {
                if (face.size() * 2u != face_bytes)
                {
                    // The packed buffer is sized from the pitch, so a face that does
                    // not match it must fail before any byte is written.
                    return AssetResult<TextureDesc>({AssetErrorCode::Value,
                                                     reference.asset_id,
                                                     location->path.utf8(),
                                                     "environment_mips",
                                                     {},
                                                     "Environment face does not match its declared slice pitch.",
                                                     {}});
                }
            }
            // Faces concatenate in cube order into one packed payload; write the
            // explicit little-endian bytes into a sized buffer instead of growing
            // it texel by texel.
            std::vector<std::uint8_t> pixels(face_bytes * environment_face_count);
            std::uint8_t* destination = pixels.data();
            for (const auto& face : loaded.value().mips[mip].faces)
            {
                const std::uint16_t* source = face.data();
                const std::uint16_t* const end = source + face.size();
                for (; source != end; ++source)
                {
                    *destination++ = static_cast<std::uint8_t>(*source & 0xffu);
                    *destination++ = static_cast<std::uint8_t>(*source >> 8u);
                }
            }
            desc.mip_pixels.push_back(std::move(pixels));
        }
        return AssetResult<TextureDesc>(std::move(desc));
    }

    AssetResult<TextureDesc> build_texture2d_desc(const FileSystem& files, const AssetIndex& index,
                                                  const AssetRef& reference)
    {
        const AssetStatus resolved = index.resolve(reference, "texture");
        if (!resolved.succeeded())
        {
            return AssetResult<TextureDesc>(resolved);
        }
        if (reference.expected_type != "toy3d.Texture2DAssetData" || reference.subresource_id.valid() ||
            reference.strength != AssetRefStrength::Strong)
        {
            return AssetResult<TextureDesc>({AssetErrorCode::TypeMismatch,
                                             reference.asset_id,
                                             {},
                                             {},
                                             "texture",
                                             "Material texture must reference a strong Texture2D root.",
                                             {}});
        }
        const AssetLocation* location = index.find(reference.asset_id);
        if (!location)
        {
            return AssetResult<TextureDesc>({AssetErrorCode::MissingReference,
                                             reference.asset_id,
                                             {},
                                             {},
                                             "texture",
                                             "Texture2D asset was not found.",
                                             {}});
        }
        const auto loaded = read_texture_asset(files, location->path);
        if (!loaded.succeeded())
        {
            return AssetResult<TextureDesc>(loaded.status());
        }
        TextureDesc desc;
        desc.usage = loaded.value().usage;
        desc.width = loaded.value().width;
        desc.height = loaded.value().height;
        desc.format = loaded.value().format;
        for (const auto& mip : loaded.value().mips)
        {
            desc.row_pitches.push_back(mip.row_pitch);
            desc.slice_pitches.push_back(mip.slice_pitch);
            desc.mip_pixels.push_back(mip.pixels);
        }
        return AssetResult<TextureDesc>(std::move(desc));
    }
} // namespace toy3d
