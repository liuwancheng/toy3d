#include "asset_file.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool read_version(const std::vector<std::uint8_t>& bytes, std::uint32_t& version)
        {
            constexpr std::uint8_t k_magic[8] = {'T', 'O', 'Y', '3', 'D', 'A', 'S', 'T'};
            if (bytes.size() < 16 || !std::equal(std::begin(k_magic), std::end(k_magic), bytes.begin()))
                return false;
            version = static_cast<std::uint32_t>(bytes[8]) |
                      (static_cast<std::uint32_t>(bytes[9]) << 8u) |
                      (static_cast<std::uint32_t>(bytes[10]) << 16u) |
                      (static_cast<std::uint32_t>(bytes[11]) << 24u);
            return true;
        }
    } // namespace

    bool AssetFormatMigrationRegistry::add_step(std::uint32_t from_version, AssetFormatStep step)
    {
        if (from_version >= asset_file_version || !step) return false;
        return steps_.emplace(from_version, std::move(step)).second;
    }

    AssetResult<std::vector<std::uint8_t>> AssetFormatMigrationRegistry::migrate(
        const std::vector<std::uint8_t>& input, AssetFileLimits limits) const
    {
        if (input.size() > limits.max_file_bytes)
            return AssetResult<std::vector<std::uint8_t>>(
                AssetStatus{AssetErrorCode::TooLarge, {}, {}, {}, {}, "asset exceeds format migration limit", {}});
        std::uint32_t version = 0;
        if (!read_version(input, version) || version > asset_file_version)
            return AssetResult<std::vector<std::uint8_t>>(
                AssetStatus{AssetErrorCode::UnsupportedVersion, {}, {}, {}, {}, "unknown asset file version", {}});
        std::vector<std::uint8_t> candidate = input;
        while (version < asset_file_version)
        {
            const auto step = steps_.find(version);
            if (step == steps_.end())
                return AssetResult<std::vector<std::uint8_t>>(
                    AssetStatus{AssetErrorCode::UnsupportedVersion, {}, {}, {}, {}, "file format migration step is missing", {}});
            auto migrated = step->second(candidate);
            if (!migrated.succeeded()) return migrated;
            std::uint32_t new_version = 0;
            if (migrated.value().size() > limits.max_file_bytes || !read_version(migrated.value(), new_version) ||
                new_version != version + 1)
                return AssetResult<std::vector<std::uint8_t>>(
                    AssetStatus{AssetErrorCode::InvalidFormat, {}, {}, {}, {}, "format step did not produce the next version", {}});
            candidate = migrated.value();
            ++version;
        }
        return AssetResult<std::vector<std::uint8_t>>(std::move(candidate));
    }
} // namespace toy3d
