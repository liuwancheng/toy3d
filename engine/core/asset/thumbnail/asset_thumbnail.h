#pragma once

#include "asset/asset_file.h"
#include "asset/asset_pair.h"
#include "misc/sha256.h"

namespace toy3d
{
    constexpr std::uint32_t thumbnail_generator_version = 2;
    constexpr std::uint32_t thumbnail_default_size = 128;
    constexpr std::uint32_t thumbnail_max_dimension = 512;
    constexpr std::size_t thumbnail_max_bytes = 4u * 1024u * 1024u;

    struct AssetThumbnailSource
    {
        std::uint32_t preview_version = 1;
        Sha256Hash content_hash{};
    };

    struct AssetThumbnailData
    {
        AssetThumbnailSource source;
        std::uint32_t generator_version = thumbnail_generator_version;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> png;
    };

    AssetResult<AssetThumbnailSource> calculate_static_mesh_thumbnail_source(
        const std::vector<std::uint8_t>& asset_bytes);
    AssetResult<AssetThumbnailSource> calculate_static_mesh_thumbnail_source(const AssetPair& pair);
    AssetResult<AssetSegmentData> encode_thumbnail_source(const AssetThumbnailSource& source);
    AssetResult<AssetThumbnailSource> decode_thumbnail_source(const std::vector<std::uint8_t>& bytes);
    AssetResult<AssetSegmentData> encode_asset_thumbnail(const AssetThumbnailData& thumbnail);
    AssetResult<AssetThumbnailData> decode_asset_thumbnail(const std::vector<std::uint8_t>& bytes);
} // namespace toy3d
