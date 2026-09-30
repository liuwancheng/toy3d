#pragma once

#include "asset_file.h"
#include "pixel_format/pixel_format.h"
#include "texture_asset/texture_asset_data.h"
#include "texture_asset_reflection.h"

#include <cstdint>
#include <vector>

namespace toy3d
{
    struct TextureAssetMip
    {
        std::uint32_t row_pitch = 0;
        std::uint32_t slice_pitch = 0;
        std::vector<std::uint8_t> pixels;
    };

    struct Texture2DAsset
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        PixelFormat format = PixelFormat::Unknown;
        std::vector<TextureAssetMip> mips;
    };

    AssetStatus validate_texture_asset(const Texture2DAsset& texture);
    AssetResult<std::vector<std::uint8_t>> encode_texture_asset(const AssetId& id,
        const Texture2DAsset& texture, std::vector<AssetSegmentData> optional_segments = {});
    AssetResult<Texture2DAsset> decode_texture_asset(const std::vector<std::uint8_t>& bytes);
    AssetResult<Texture2DAsset> read_texture_asset(const FileSystem& files, const VirtualPath& path);
} // namespace toy3d
