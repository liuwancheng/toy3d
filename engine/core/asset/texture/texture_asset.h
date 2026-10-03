#pragma once

#include "asset/asset_file.h"
#include "asset/asset_pair.h"
#include "image/pixel_format.h"
#include "image/texture_usage.h"
#include "asset/texture/texture_asset_data.h"
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
        TextureUsage usage = TextureUsage::Color;
        bool flip_green = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        PixelFormat format = PixelFormat::Unknown;
        std::vector<TextureAssetMip> mips;
    };

    AssetStatus validate_texture_asset(const Texture2DAsset& texture);
    AssetResult<std::vector<std::uint8_t>> encode_texture_asset(const AssetId& id, const Texture2DAsset& texture,
                                                                std::vector<AssetSegmentData> optional_segments = {});
    AssetResult<AssetPairBytes> encode_texture_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                          const Texture2DAsset& texture);
    AssetResult<Texture2DAsset> decode_texture_asset(const std::vector<std::uint8_t>& bytes);
    AssetResult<Texture2DAsset> read_texture_asset(const FileSystem& files, const VirtualPath& path);
} // namespace toy3d
