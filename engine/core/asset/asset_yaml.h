#pragma once

#include "asset_file.h"
#include "misc/sha256.h"

namespace toy3d
{
    struct AssetYamlDocument
    {
        AssetFileIndex index;
        std::vector<std::uint8_t> type_data;
        bool has_meta = false;
        std::uint64_t meta_size = 0;
        Sha256Hash meta_hash{};
        std::vector<std::string> meta_segments;
    };

    AssetResult<std::vector<std::uint8_t>> encode_asset_yaml(
        const TypeRegistry& types, const AssetYamlDocument& document,
        AssetFileLimits limits = {}, ValueLimits value_limits = {});
    AssetResult<AssetYamlDocument> decode_asset_yaml(
        const TypeRegistry& types, const std::vector<std::uint8_t>& bytes,
        AssetFileLimits limits = {}, ValueLimits value_limits = {});
}
