#include "asset_file.h"

#include <algorithm>
#include <limits>
#include <set>

namespace toy3d
{
    namespace
    {
        constexpr std::uint8_t k_magic[8] = {'T', 'O', 'Y', '3', 'D', 'A', 'S', 'T'};
        constexpr std::uint64_t k_header_bytes = 16;

        AssetStatus fail(AssetErrorCode code, const char* message, const AssetFileIndex& index = {})
        {
            return {code, index.asset_id, {}, {}, {}, message, {}};
        }

        ValueStatus write_id(ValueWriter& writer, const std::array<std::uint8_t, 16>& bytes)
        {
            for (std::uint8_t byte : bytes)
            {
                ValueStatus status = writer.write_uint8(byte);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }

        ValueStatus read_id(ValueReader& reader, std::array<std::uint8_t, 16>& bytes)
        {
            for (std::uint8_t& byte : bytes)
            {
                ValueStatus status = reader.read_uint8(byte);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }

        ValueStatus write_index(ValueWriter& writer, const AssetFileIndex& index)
        {
            ValueStatus status = write_id(writer, index.asset_id.bytes);
            if (!status.succeeded())
            {
                return status;
            }
            status = writer.write_utf8(index.root_type);
            if (!status.succeeded())
            {
                return status;
            }
            status = writer.write_uint32(index.schema_version);
            if (!status.succeeded())
            {
                return status;
            }
            status = writer.write_array_length(static_cast<std::uint32_t>(index.dependencies.size()));
            if (!status.succeeded())
            {
                return status;
            }
            for (const AssetRef& dependency : index.dependencies)
            {
                status = write_id(writer, dependency.asset_id.bytes);
                if (!status.succeeded())
                {
                    return status;
                }
                status = write_id(writer, dependency.subresource_id.bytes);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_utf8(dependency.expected_type);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_uint8(static_cast<std::uint8_t>(dependency.strength));
                if (!status.succeeded())
                {
                    return status;
                }
            }
            status = writer.write_array_length(static_cast<std::uint32_t>(index.subresources.size()));
            if (!status.succeeded())
            {
                return status;
            }
            for (const AssetSubresource& subresource : index.subresources)
            {
                status = write_id(writer, subresource.id.bytes);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_utf8(subresource.type_name);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            status = writer.write_array_length(static_cast<std::uint32_t>(index.segments.size()));
            if (!status.succeeded())
            {
                return status;
            }
            for (const AssetSegment& segment : index.segments)
            {
                status = writer.write_utf8(segment.name);
                if (!status.succeeded())
                {
                    return status;
                }
                status = writer.write_uint8(segment.kind);
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

        ValueStatus read_index(ValueReader& reader, AssetFileIndex& index)
        {
            ValueStatus status = read_id(reader, index.asset_id.bytes);
            if (!status.succeeded())
            {
                return status;
            }
            status = reader.read_utf8(index.root_type);
            if (!status.succeeded())
            {
                return status;
            }
            status = reader.read_uint32(index.schema_version);
            if (!status.succeeded())
            {
                return status;
            }
            std::uint32_t count = 0;
            status = reader.read_array_length(count);
            if (!status.succeeded())
            {
                return status;
            }
            for (std::uint32_t item = 0; item < count; ++item)
            {
                AssetRef dependency;
                status = read_id(reader, dependency.asset_id.bytes);
                if (!status.succeeded())
                {
                    return status;
                }
                status = read_id(reader, dependency.subresource_id.bytes);
                if (!status.succeeded())
                {
                    return status;
                }
                status = reader.read_utf8(dependency.expected_type);
                if (!status.succeeded())
                {
                    return status;
                }
                std::uint8_t strength = 0;
                status = reader.read_uint8(strength);
                if (!status.succeeded())
                {
                    return status;
                }
                if (strength > 2)
                {
                    return reader.failure(ValueErrorCode::InvalidValue, "unknown dependency strength");
                }
                dependency.strength = static_cast<AssetRefStrength>(strength);
                index.dependencies.push_back(std::move(dependency));
            }
            status = reader.read_array_length(count);
            if (!status.succeeded())
            {
                return status;
            }
            for (std::uint32_t item = 0; item < count; ++item)
            {
                AssetSubresource subresource;
                status = read_id(reader, subresource.id.bytes);
                if (!status.succeeded())
                {
                    return status;
                }
                status = reader.read_utf8(subresource.type_name);
                if (!status.succeeded())
                {
                    return status;
                }
                index.subresources.push_back(std::move(subresource));
            }
            status = reader.read_array_length(count);
            if (!status.succeeded())
            {
                return status;
            }
            for (std::uint32_t item = 0; item < count; ++item)
            {
                AssetSegment segment;
                status = reader.read_utf8(segment.name);
                if (!status.succeeded())
                {
                    return status;
                }
                status = reader.read_uint8(segment.kind);
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
                index.segments.push_back(std::move(segment));
            }
            if (!reader.at_end())
            {
                return reader.failure(ValueErrorCode::InvalidValue, "trailing index bytes");
            }
            return ValueStatus::success();
        }

        AssetStatus validate_index(const AssetFileIndex& index, std::uint64_t content_start, std::uint64_t file_size,
                                   AssetFileLimits limits)
        {
            if (!index.asset_id.valid() || index.root_type.empty() || index.schema_version == 0 ||
                index.dependencies.size() > limits.max_entries || index.subresources.size() > limits.max_entries ||
                index.segments.size() > limits.max_entries)
            {
                return fail(AssetErrorCode::InvalidFormat, "invalid asset identity, schema or index count", index);
            }
            std::set<SubresourceId> subresource_ids;
            for (const AssetSubresource& subresource : index.subresources)
            {
                if (!subresource.id.valid() || subresource.type_name.empty() ||
                    !subresource_ids.insert(subresource.id).second)
                {
                    return fail(AssetErrorCode::InvalidFormat, "invalid or duplicate subresource", index);
                }
            }
            for (const AssetRef& dependency : index.dependencies)
            {
                if (!dependency.asset_id.valid() || dependency.expected_type.empty())
                {
                    return fail(AssetErrorCode::InvalidFormat, "invalid dependency", index);
                }
            }
            std::set<std::string> names;
            std::vector<AssetSegment> sorted = index.segments;
            std::sort(sorted.begin(), sorted.end(),
                      [](const AssetSegment& left, const AssetSegment& right)
                      {
                          return left.offset < right.offset;
                      });
            std::uint64_t end = content_start;
            bool has_type_data = false;
            for (const AssetSegment& segment : sorted)
            {
                if (segment.name.empty() || !names.insert(segment.name).second || segment.offset < end ||
                    segment.offset > file_size || segment.length > file_size - segment.offset)
                {
                    AssetStatus status = fail(AssetErrorCode::InvalidFormat, "invalid or overlapping segment", index);
                    status.segment = segment.name;
                    return status;
                }
                if (segment.kind != 1 && segment.kind != 2 && segment.required)
                {
                    AssetStatus status =
                        fail(AssetErrorCode::UnknownRequiredSegment, "unknown required segment", index);
                    status.segment = segment.name;
                    return status;
                }
                if (segment.name == "type_data" && segment.kind == 1 && segment.required)
                {
                    has_type_data = true;
                }
                end = segment.offset + segment.length;
            }
            if (!has_type_data)
            {
                return fail(AssetErrorCode::InvalidFormat, "required type_data segment missing", index);
            }
            return AssetStatus::success();
        }
    } // namespace

    AssetResult<std::vector<std::uint8_t>> encode_asset_file(AssetFileIndex index, std::vector<AssetSegmentData> data,
                                                             AssetFileLimits limits)
    {
        if (data.size() > limits.max_entries || index.dependencies.size() > limits.max_entries ||
            index.subresources.size() > limits.max_entries)
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::TooLarge, "index entry limit exceeded", index));
        }
        std::sort(index.dependencies.begin(), index.dependencies.end(),
                  [](const AssetRef& left, const AssetRef& right)
                  {
                      return left.asset_id < right.asset_id;
                  });
        std::sort(index.subresources.begin(), index.subresources.end(),
                  [](const AssetSubresource& left, const AssetSubresource& right)
                  {
                      return left.id < right.id;
                  });
        std::sort(data.begin(), data.end(),
                  [](const AssetSegmentData& left, const AssetSegmentData& right)
                  {
                      return left.name < right.name;
                  });
        index.segments.clear();
        for (const AssetSegmentData& segment : data)
        {
            index.segments.push_back({segment.name, segment.kind, segment.required, 0, segment.bytes.size()});
        }
        ValueLimits value_limits;
        value_limits.max_bytes = limits.max_index_bytes;
        value_limits.max_array_elements = limits.max_entries;
        ValueWriter dry(value_limits);
        ValueStatus value_status = write_index(dry, index);
        if (!value_status.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::TooLarge, value_status.message.c_str(), index));
        }
        std::uint64_t offset = k_header_bytes + dry.bytes().size();
        for (AssetSegment& segment : index.segments)
        {
            segment.offset = offset;
            if (segment.length > limits.max_file_bytes || offset > limits.max_file_bytes - segment.length)
            {
                return AssetResult<std::vector<std::uint8_t>>(
                    fail(AssetErrorCode::TooLarge, "asset exceeds file limit", index));
            }
            offset += segment.length;
        }
        if (offset > std::numeric_limits<std::size_t>::max())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::TooLarge, "asset exceeds host address space", index));
        }
        AssetStatus checked = validate_index(index, k_header_bytes + dry.bytes().size(), offset, limits);
        if (!checked.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(checked);
        }
        ValueWriter encoded(value_limits);
        value_status = write_index(encoded, index);
        if (!value_status.succeeded() || encoded.bytes().size() != dry.bytes().size())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::InvalidFormat, "index encoding changed size", index));
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(offset));
        bytes.insert(bytes.end(), std::begin(k_magic), std::end(k_magic));
        ValueWriter header;
        value_status = header.write_uint32(asset_file_version);
        if (!value_status.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::InvalidFormat, "header encoding failed", index));
        }
        value_status = header.write_uint32(static_cast<std::uint32_t>(encoded.bytes().size()));
        if (!value_status.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                fail(AssetErrorCode::InvalidFormat, "header encoding failed", index));
        }
        bytes.insert(bytes.end(), header.bytes().begin(), header.bytes().end());
        bytes.insert(bytes.end(), encoded.bytes().begin(), encoded.bytes().end());
        for (const AssetSegmentData& segment : data)
        {
            bytes.insert(bytes.end(), segment.bytes.begin(), segment.bytes.end());
        }
        return AssetResult<std::vector<std::uint8_t>>(std::move(bytes));
    }

    AssetResult<AssetFileIndex> inspect_asset(const FileSystem& files, const VirtualPath& path, AssetFileLimits limits)
    {
        auto opened = files.open(path, FileOpenMode::Read);
        if (!opened.succeeded())
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::Io, {}, path.utf8(), {}, {}, "asset open failed", opened.status()});
        }
        auto& handle = *opened.value();
        auto size = handle.size();
        if (!size.succeeded())
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::Io, {}, path.utf8(), {}, {}, "asset stat failed", size.status()});
        }
        if (size.value() < k_header_bytes || size.value() > limits.max_file_bytes)
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::TooLarge, {}, path.utf8(), {}, {}, "asset size outside limits", {}});
        }
        std::vector<std::uint8_t> header(static_cast<std::size_t>(k_header_bytes));
        auto head_read = handle.read_at(0, header.data(), header.size());
        if (!head_read.succeeded() || head_read.value() != header.size())
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::Io, {}, path.utf8(), {}, {}, "short asset header read", head_read.status()});
        }
        if (!std::equal(std::begin(k_magic), std::end(k_magic), header.begin()))
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::InvalidFormat, {}, path.utf8(), {}, {}, "bad asset magic", {}});
        }
        std::vector<std::uint8_t> control(header.begin() + 8, header.end());
        ValueReader head_reader(control);
        std::uint32_t version = 0;
        std::uint32_t index_length = 0;
        const ValueStatus version_status = head_reader.read_uint32(version);
        const ValueStatus length_status = head_reader.read_uint32(index_length);
        if (!version_status.succeeded() || !length_status.succeeded())
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::InvalidFormat, {}, path.utf8(), {}, {}, "invalid asset header", {}});
        }
        if (version != asset_file_version)
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::UnsupportedVersion, {}, path.utf8(), {}, {}, "unsupported asset file version", {}});
        }
        if (index_length > limits.max_index_bytes || index_length > size.value() - k_header_bytes)
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::InvalidFormat, {}, path.utf8(), {}, {}, "invalid index length", {}});
        }
        std::vector<std::uint8_t> index_bytes(index_length);
        auto index_read = handle.read_at(k_header_bytes, index_bytes.data(), index_bytes.size());
        if (!index_read.succeeded() || index_read.value() != index_bytes.size())
        {
            return AssetResult<AssetFileIndex>(
                {AssetErrorCode::Io, {}, path.utf8(), {}, {}, "short asset index read", index_read.status()});
        }
        ValueLimits value_limits;
        value_limits.max_bytes = limits.max_index_bytes;
        value_limits.max_array_elements = limits.max_entries;
        ValueReader reader(index_bytes, value_limits);
        AssetFileIndex index;
        ValueStatus value_status = read_index(reader, index);
        if (!value_status.succeeded())
        {
            return AssetResult<AssetFileIndex>({AssetErrorCode::InvalidFormat,
                                                index.asset_id,
                                                path.utf8(),
                                                {},
                                                value_status.property_path,
                                                value_status.message,
                                                {}});
        }
        AssetStatus checked = validate_index(index, k_header_bytes + index_length, size.value(), limits);
        if (!checked.succeeded())
        {
            checked.virtual_path = path.utf8();
            return AssetResult<AssetFileIndex>(checked);
        }
        return AssetResult<AssetFileIndex>(std::move(index));
    }

    AssetResult<std::vector<std::uint8_t>> read_asset_segment(const FileSystem& files, const VirtualPath& path,
                                                              const AssetId& id, const AssetSegment& segment,
                                                              std::size_t max_bytes)
    {
        if (segment.length > max_bytes || segment.length > std::numeric_limits<std::size_t>::max())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                {AssetErrorCode::TooLarge, id, path.utf8(), segment.name, {}, "asset segment exceeds read limit", {}});
        }
        auto opened = files.open(path, FileOpenMode::Read);
        if (!opened.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                {AssetErrorCode::Io, id, path.utf8(), segment.name, {}, "asset segment open failed", opened.status()});
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(segment.length));
        auto read = opened.value()->read_at(segment.offset, bytes.data(), bytes.size());
        if (!read.succeeded() || read.value() != bytes.size())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                {AssetErrorCode::Io, id, path.utf8(), segment.name, {}, "short asset segment read", read.status()});
        }
        const FileStatus closed = opened.value()->close();
        if (!closed.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(
                {AssetErrorCode::Io, id, path.utf8(), segment.name, {}, "asset segment close failed", closed});
        }
        return AssetResult<std::vector<std::uint8_t>>(std::move(bytes));
    }

    AssetResult<AssetFileIndex> inspect_asset_bytes(const std::vector<std::uint8_t>& bytes, AssetFileLimits limits)
    {
        if (bytes.size() < k_header_bytes || bytes.size() > limits.max_file_bytes)
        {
            return AssetResult<AssetFileIndex>(fail(AssetErrorCode::TooLarge, "asset byte count outside limits"));
        }
        if (!std::equal(std::begin(k_magic), std::end(k_magic), bytes.begin()))
        {
            return AssetResult<AssetFileIndex>(fail(AssetErrorCode::InvalidFormat, "bad asset magic"));
        }
        const std::vector<std::uint8_t> control(bytes.begin() + 8, bytes.begin() + 16);
        ValueReader header(control);
        std::uint32_t version = 0;
        std::uint32_t index_size = 0;
        if (!header.read_uint32(version).succeeded() || !header.read_uint32(index_size).succeeded())
        {
            return AssetResult<AssetFileIndex>(fail(AssetErrorCode::InvalidFormat, "invalid asset header"));
        }
        if (version != asset_file_version)
        {
            return AssetResult<AssetFileIndex>(
                fail(AssetErrorCode::UnsupportedVersion, "unsupported asset file version"));
        }
        if (index_size > limits.max_index_bytes || index_size > bytes.size() - k_header_bytes)
        {
            return AssetResult<AssetFileIndex>(fail(AssetErrorCode::InvalidFormat, "invalid asset index length"));
        }
        const std::vector<std::uint8_t> index_bytes(
            bytes.begin() + 16, bytes.begin() + static_cast<std::ptrdiff_t>(k_header_bytes + index_size));
        ValueLimits value_limits;
        value_limits.max_bytes = limits.max_index_bytes;
        value_limits.max_array_elements = limits.max_entries;
        ValueReader reader(index_bytes, value_limits);
        AssetFileIndex index;
        const ValueStatus parsed = read_index(reader, index);
        if (!parsed.succeeded())
        {
            return AssetResult<AssetFileIndex>(fail(AssetErrorCode::InvalidFormat, parsed.message.c_str(), index));
        }
        const AssetStatus checked = validate_index(index, k_header_bytes + index_size, bytes.size(), limits);
        if (!checked.succeeded())
        {
            return AssetResult<AssetFileIndex>(checked);
        }
        return AssetResult<AssetFileIndex>(std::move(index));
    }
    AssetResult<std::vector<std::uint8_t>> replace_asset_segments(const std::vector<std::uint8_t>& original,
                                                                  const std::vector<AssetSegmentData>& replacements,
                                                                  AssetFileLimits limits)
    {
        const auto index = inspect_asset_bytes(original, limits);
        if (!index.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(index.status());
        }
        std::vector<AssetSegmentData> segments;
        for (const AssetSegment& existing : index.value().segments)
        {
            const auto replacement = std::find_if(replacements.begin(), replacements.end(),
                                                  [&existing](const AssetSegmentData& value)
                                                  {
                                                      return value.name == existing.name;
                                                  });
            if (replacement != replacements.end())
            {
                continue;
            }
            segments.push_back(
                {existing.name, existing.kind, existing.required,
                 std::vector<std::uint8_t>(original.begin() + static_cast<std::ptrdiff_t>(existing.offset),
                                           original.begin() +
                                               static_cast<std::ptrdiff_t>(existing.offset + existing.length))});
        }
        segments.insert(segments.end(), replacements.begin(), replacements.end());
        return encode_asset_file(index.value(), std::move(segments), limits);
    }
} // namespace toy3d
