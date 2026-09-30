#pragma once

#include "asset_file.h"

namespace toy3d
{
    struct AssetMetaFile
    {
        AssetId asset_id;
        std::vector<AssetSegmentData> segments;
    };

    AssetResult<std::vector<std::uint8_t>> encode_asset_meta(
        AssetMetaFile file, AssetFileLimits limits = {});
    AssetResult<AssetMetaFile> decode_asset_meta(
        const std::vector<std::uint8_t>& bytes, AssetFileLimits limits = {});
}
