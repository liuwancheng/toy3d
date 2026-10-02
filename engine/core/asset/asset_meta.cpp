#include "asset_meta.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr std::uint8_t k_magic[8] = {'T', 'O', 'Y', '3', 'D', 'M', 'T', 'A'};
        constexpr std::uint32_t k_version = 1u;
        constexpr std::uint64_t k_header_size = 16u;

        AssetStatus fail(AssetErrorCode code, const AssetId& id, const char* message)
        {
            return {code, id, {}, {}, {}, message, {}};
        }

        ValueStatus write_index(ValueWriter& writer, const AssetId& id, const std::vector<AssetSegment>& segments)
        {
            for (std::uint8_t byte : id.bytes)
            {
                const ValueStatus status = writer.write_uint8(byte);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            ValueStatus status = writer.write_array_length(static_cast<std::uint32_t>(segments.size()));
            if (!status.succeeded())
            {
                return status;
            }
            for (const AssetSegment& segment : segments)
            {
                status = writer.write_utf8(segment.name);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_bool(segment.required);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_uint64(segment.offset);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_uint64(segment.length);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }

        ValueStatus read_index(ValueReader& reader, AssetId& id, std::vector<AssetSegment>& segments)
        {
            for (std::uint8_t& byte : id.bytes)
            {
                const ValueStatus status = reader.read_uint8(byte);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            std::uint32_t count = 0;
            ValueStatus status = reader.read_array_length(count);
            if (!status.succeeded())
            {
                return status;
            }
            segments.reserve(count);
            for (std::uint32_t item = 0; item < count; ++item)
            {
                AssetSegment segment;
                segment.kind = 2u;
                status = reader.read_utf8(segment.name);
                if (!status.succeeded())
                {
                    return status;
                }
                status = reader.read_bool(segment.required);
                if (!status.succeeded())
                {
                    return status;
                }
                status = reader.read_uint64(segment.offset);
                if (!status.succeeded())
                {
                    return status;
                }
                status = reader.read_uint64(segment.length);
                if (!status.succeeded())
                {
                    return status;
                }
                segments.push_back(std::move(segment));
            }
            return ValueStatus::success();
        }

        AssetStatus validate(const AssetId& id, const std::vector<AssetSegment>& segments, std::uint64_t data_start,
                             std::uint64_t file_size, AssetFileLimits limits)
        {
            if (!id.valid() || segments.empty() || segments.size() > limits.max_entries)
            {
                return fail(AssetErrorCode::InvalidFormat, id, "invalid meta identity or segment count");
            }
            std::set<std::string> names;
            std::vector<AssetSegment> ordered = segments;
            std::sort(ordered.begin(), ordered.end(),
                      [](const AssetSegment& left, const AssetSegment& right)
                      {
                          return left.offset < right.offset;
                      });
            std::uint64_t cursor = data_start;
            for (const AssetSegment& segment : ordered)
            {
                if (segment.name.empty() || !names.insert(segment.name).second || segment.offset != cursor ||
                    segment.offset > file_size || segment.length > file_size - segment.offset)
                {
                    return fail(AssetErrorCode::InvalidFormat, id, "invalid meta segment directory");
                }
                cursor += segment.length;
            }
            if (cursor != file_size)
            {
                return fail(AssetErrorCode::InvalidFormat, id, "trailing meta bytes");
            }
            return AssetStatus::success();
        }
    } // namespace

    AssetResult<std::vector<std::uint8_t>> encode_asset_meta(AssetMetaFile file, AssetFileLimits limits)
    {
        if (!file.asset_id.valid() || file.segments.empty() || file.segments.size() > limits.max_entries ||
            file.segments.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::InvalidFormat, file.asset_id, "invalid meta input"));
        }
        std::sort(file.segments.begin(), file.segments.end(),
                  [](const AssetSegmentData& left, const AssetSegmentData& right)
                  {
                      return left.name < right.name;
                  });
        std::vector<AssetSegment> directory;
        directory.reserve(file.segments.size());
        for (const AssetSegmentData& segment : file.segments)
        {
            if (segment.kind != 2u)
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(AssetErrorCode::InvalidFormat, file.asset_id, "meta only accepts binary segments"));
            }
            directory.push_back({segment.name, 2u, segment.required, 0u, segment.bytes.size()});
        }
        ValueLimits value_limits;
        value_limits.max_bytes = limits.max_index_bytes;
        value_limits.max_array_elements = limits.max_entries;
        ValueWriter dry(value_limits);
        const ValueStatus dry_status = write_index(dry, file.asset_id, directory);
        if (!dry_status.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::TooLarge, file.asset_id, "meta index exceeds limit"));
        }
        std::uint64_t size = k_header_size + dry.bytes().size();
        for (AssetSegment& segment : directory)
        {
            segment.offset = size;
            if (size > limits.max_file_bytes || segment.length > limits.max_file_bytes - size)
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(AssetErrorCode::TooLarge, file.asset_id, "meta exceeds file limit"));
            }
            size += segment.length;
        }
        const AssetStatus valid = validate(file.asset_id, directory, k_header_size + dry.bytes().size(), size, limits);
        if (!valid.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(valid);
        }
        ValueWriter index(value_limits);
        const ValueStatus indexed = write_index(index, file.asset_id, directory);
        if (!indexed.succeeded() || index.bytes().size() != dry.bytes().size())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::InvalidFormat, file.asset_id, "meta index encoding changed size"));
        }
        ValueWriter header;
        if (!header.write_uint32(k_version).succeeded() ||
            !header.write_uint32(static_cast<std::uint32_t>(index.bytes().size())).succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::InvalidFormat, file.asset_id, "meta header encoding failed"));
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(size));
        bytes.insert(bytes.end(), std::begin(k_magic), std::end(k_magic));
        bytes.insert(bytes.end(), header.bytes().begin(), header.bytes().end());
        bytes.insert(bytes.end(), index.bytes().begin(), index.bytes().end());
        for (const AssetSegmentData& segment : file.segments)
        {
            bytes.insert(bytes.end(), segment.bytes.begin(), segment.bytes.end());
        }
        return AssetResult<std::vector<std::uint8_t>>(std::move(bytes));
    }

    AssetResult<AssetMetaFile> decode_asset_meta(const std::vector<std::uint8_t>& bytes, AssetFileLimits limits)
    {
        if (bytes.size() < k_header_size || bytes.size() > limits.max_file_bytes ||
            !std::equal(std::begin(k_magic), std::end(k_magic), bytes.begin()))
        {
            return AssetResult<AssetMetaFile>(fail(AssetErrorCode::InvalidFormat, {}, "invalid meta header"));
        }
        const std::vector<std::uint8_t> control(bytes.begin() + 8u, bytes.begin() + 16u);
        ValueReader header(control);
        std::uint32_t version = 0;
        std::uint32_t index_size = 0;
        if (!header.read_uint32(version).succeeded() || !header.read_uint32(index_size).succeeded())
        {
            return AssetResult<AssetMetaFile>(fail(AssetErrorCode::InvalidFormat, {}, "invalid meta header"));
        }
        if (version != k_version)
        {
            return AssetResult<AssetMetaFile>(fail(AssetErrorCode::UnsupportedVersion, {}, "unsupported meta version"));
        }
        if (index_size > limits.max_index_bytes || index_size > bytes.size() - k_header_size)
        {
            return AssetResult<AssetMetaFile>(fail(AssetErrorCode::InvalidFormat, {}, "invalid meta index length"));
        }
        std::vector<std::uint8_t> index_bytes(bytes.begin() + 16u,
                                              bytes.begin() + static_cast<std::ptrdiff_t>(k_header_size + index_size));
        ValueLimits value_limits;
        value_limits.max_bytes = limits.max_index_bytes;
        value_limits.max_array_elements = limits.max_entries;
        ValueReader reader(index_bytes, value_limits);
        AssetMetaFile file;
        std::vector<AssetSegment> directory;
        const ValueStatus parsed = read_index(reader, file.asset_id, directory);
        if (!parsed.succeeded() || !reader.at_end())
        {
            return AssetResult<AssetMetaFile>(fail(AssetErrorCode::InvalidFormat, file.asset_id, "invalid meta index"));
        }
        const AssetStatus valid = validate(file.asset_id, directory, k_header_size + index_size, bytes.size(), limits);
        if (!valid.succeeded())
        {
            return AssetResult<AssetMetaFile>(valid);
        }
        file.segments.reserve(directory.size());
        for (const AssetSegment& segment : directory)
        {
            file.segments.push_back(
                {segment.name, 2u, segment.required,
                 std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(segment.offset),
                                           bytes.begin() +
                                               static_cast<std::ptrdiff_t>(segment.offset + segment.length))});
        }
        return AssetResult<AssetMetaFile>(std::move(file));
    }
} // namespace toy3d
