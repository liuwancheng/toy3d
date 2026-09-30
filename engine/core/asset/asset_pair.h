#pragma once

#include "asset_meta.h"
#include "asset_yaml.h"

namespace toy3d
{
    struct AssetPairBytes
    {
        std::vector<std::uint8_t> asset;
        std::vector<std::uint8_t> meta;
        bool has_meta = false;
    };

    struct AssetPair
    {
        AssetYamlDocument description;
        AssetMetaFile meta;
    };

    AssetResult<AssetPairBytes> encode_asset_pair(const TypeRegistry& types,
        AssetFileIndex index, std::vector<std::uint8_t> type_data,
        std::vector<AssetSegmentData> payloads, AssetFileLimits limits = {},
        ValueLimits value_limits = {});
    AssetResult<AssetPair> read_asset_pair(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& asset_path, AssetFileLimits limits = {},
        ValueLimits value_limits = {});
}
