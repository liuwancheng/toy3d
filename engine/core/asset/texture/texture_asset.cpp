#include "asset/texture/texture_asset.h"

#include "asset/asset_pair.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr std::uint32_t maximum_dimension = 4096u;
        constexpr std::size_t maximum_payload = 128u * 1024u * 1024u;
        constexpr std::size_t maximum_file = maximum_payload + 1024u * 1024u;
        constexpr std::uint32_t mip_format_version = 1u;

        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "texture_mips", {}, message, {}};
        }

        std::uint32_t full_mip_count(std::uint32_t width, std::uint32_t height)
        {
            std::uint32_t extent = std::max(width, height);
            std::uint32_t count = 0;
            while (extent != 0)
            {
                ++count;
                extent >>= 1u;
            }
            return count;
        }

        void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
        {
            for (unsigned shift = 0; shift != 32; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }

        bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, std::size_t end,
                      std::uint32_t& value)
        {
            if (cursor > end || end > bytes.size() || end - cursor < 4u)
            {
                return false;
            }
            value = 0;
            for (unsigned shift = 0; shift != 32; shift += 8)
            {
                value |= static_cast<std::uint32_t>(bytes[cursor++]) << shift;
            }
            return true;
        }
    } // namespace

    AssetStatus validate_texture_asset(const Texture2DAsset& texture)
    {
        if (texture.width == 0 || texture.height == 0 || texture.width > maximum_dimension ||
            texture.height > maximum_dimension || !texture_usage_matches_format(texture.usage, texture.format) ||
            (texture.flip_green && texture.usage != TextureUsage::Normal) ||
            texture.mips.size() != full_mip_count(texture.width, texture.height))
        {
            return invalid(
                "Texture2D requires an explicit compatible usage/format, bounded dimensions and a complete mip chain.");
        }
        std::size_t total = 0;
        for (std::size_t level = 0; level < texture.mips.size(); ++level)
        {
            const auto& mip = texture.mips[level];
            const std::uint32_t width = std::max(1u, texture.width >> static_cast<unsigned>(level));
            const std::uint32_t height = std::max(1u, texture.height >> static_cast<unsigned>(level));
            const std::uint32_t row = width * 4u;
            const std::uint32_t slice = row * height;
            if (mip.row_pitch != row || mip.slice_pitch != slice || mip.pixels.size() != slice ||
                slice > maximum_payload - total)
            {
                return invalid("Texture2D mip pitch, length or payload budget is invalid.");
            }
            total += slice;
        }
        return AssetStatus::success();
    }

    AssetResult<std::vector<std::uint8_t>> encode_texture_asset(const AssetId& id, const Texture2DAsset& texture,
                                                                std::vector<AssetSegmentData> optional_segments)
    {
        const AssetStatus valid = validate_texture_asset(texture);
        if (!valid.succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(valid);
        }
        if (!id.valid())
        {
            return AssetResult<std::vector<std::uint8_t>>(invalid("Texture2D needs a valid asset ID."));
        }
        Texture2DAssetData metadata;
        metadata.usage = static_cast<std::uint32_t>(texture.usage);
        metadata.flip_green = texture.flip_green;
        metadata.width = texture.width;
        metadata.height = texture.height;
        metadata.pixel_format = static_cast<std::uint32_t>(texture.format);
        metadata.mip_count = static_cast<std::uint32_t>(texture.mips.size());
        ValueWriter writer;
        if (!encode_value(writer, metadata).succeeded())
        {
            return AssetResult<std::vector<std::uint8_t>>(invalid("Texture2D metadata encoding failed."));
        }
        std::vector<std::uint8_t> payload;
        append_u32(payload, mip_format_version);
        append_u32(payload, metadata.mip_count);
        for (const auto& mip : texture.mips)
        {
            append_u32(payload, mip.row_pitch);
            append_u32(payload, mip.slice_pitch);
            append_u32(payload, static_cast<std::uint32_t>(mip.pixels.size()));
            payload.insert(payload.end(), mip.pixels.begin(), mip.pixels.end());
        }
        optional_segments.push_back({"type_data", 1, true, writer.bytes()});
        optional_segments.push_back({"texture_mips", 2, true, std::move(payload)});
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.Texture2DAssetData";
        index.schema_version = 2u;
        AssetFileLimits limits;
        limits.max_file_bytes = maximum_file;
        return encode_asset_file(std::move(index), std::move(optional_segments), limits);
    }

    AssetResult<Texture2DAsset> decode_texture_asset(const std::vector<std::uint8_t>& bytes)
    {
        AssetFileLimits limits;
        limits.max_file_bytes = maximum_file;
        const auto index = inspect_asset_bytes(bytes, limits);
        if (!index.succeeded())
        {
            return AssetResult<Texture2DAsset>(index.status());
        }
        if (index.value().root_type != "toy3d.Texture2DAssetData" || index.value().schema_version != 2u ||
            !index.value().dependencies.empty() || !index.value().subresources.empty())
        {
            return AssetResult<Texture2DAsset>(invalid("Unsupported Texture2D asset root or dependencies."));
        }
        const AssetSegment* typed = nullptr;
        const AssetSegment* mips = nullptr;
        for (const auto& segment : index.value().segments)
        {
            if (segment.required && segment.name != "type_data" && segment.name != "texture_mips")
            {
                return AssetResult<Texture2DAsset>(invalid("Unknown required Texture2D segment."));
            }
            if (segment.name == "type_data")
            {
                typed = &segment;
            }
            if (segment.name == "texture_mips")
            {
                mips = &segment;
            }
        }
        if (!typed || !mips || !typed->required || !mips->required || typed->kind != 1u || mips->kind != 2u ||
            typed->length > ValueLimits{}.max_bytes || mips->length > maximum_payload + 256u)
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D segment declaration is invalid."));
        }
        const std::vector<std::uint8_t> metadata_bytes(bytes.begin() + static_cast<std::ptrdiff_t>(typed->offset),
                                                       bytes.begin() +
                                                           static_cast<std::ptrdiff_t>(typed->offset + typed->length));
        ValueReader reader(metadata_bytes);
        Texture2DAssetData metadata;
        if (!decode_value(reader, metadata).succeeded() || !reader.at_end() || metadata.width == 0 ||
            metadata.height == 0 || metadata.width > maximum_dimension || metadata.height > maximum_dimension ||
            metadata.pixel_format >= static_cast<std::uint32_t>(PixelFormat::Max) ||
            !texture_usage_matches_format(static_cast<TextureUsage>(metadata.usage),
                                          static_cast<PixelFormat>(metadata.pixel_format)) ||
            (metadata.flip_green && metadata.usage != static_cast<std::uint32_t>(TextureUsage::Normal)) ||
            metadata.mip_count != full_mip_count(metadata.width, metadata.height))
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D metadata is invalid."));
        }
        std::size_t cursor = static_cast<std::size_t>(mips->offset);
        const std::size_t end = static_cast<std::size_t>(mips->offset + mips->length);
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (!read_u32(bytes, cursor, end, version) || !read_u32(bytes, cursor, end, count) ||
            version != mip_format_version || count != metadata.mip_count)
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D mip version or count is invalid."));
        }
        Texture2DAsset candidate;
        candidate.width = metadata.width;
        candidate.height = metadata.height;
        candidate.usage = static_cast<TextureUsage>(metadata.usage);
        candidate.flip_green = metadata.flip_green;
        candidate.format = static_cast<PixelFormat>(metadata.pixel_format);
        candidate.mips.reserve(count);
        for (std::uint32_t level = 0; level < count; ++level)
        {
            TextureAssetMip mip;
            std::uint32_t length = 0;
            if (!read_u32(bytes, cursor, end, mip.row_pitch) || !read_u32(bytes, cursor, end, mip.slice_pitch) ||
                !read_u32(bytes, cursor, end, length) || length > end - cursor)
            {
                return AssetResult<Texture2DAsset>(invalid("Texture2D mip payload is truncated."));
            }
            mip.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                              bytes.begin() + static_cast<std::ptrdiff_t>(cursor + length));
            cursor += length;
            candidate.mips.push_back(std::move(mip));
        }
        if (cursor != end)
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D mip payload has trailing bytes."));
        }
        const AssetStatus valid = validate_texture_asset(candidate);
        if (!valid.succeeded())
        {
            return AssetResult<Texture2DAsset>(valid);
        }
        return AssetResult<Texture2DAsset>(std::move(candidate));
    }

    AssetResult<AssetPairBytes> encode_texture_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                          const Texture2DAsset& texture)
    {
        const AssetStatus valid = validate_texture_asset(texture);
        if (!valid.succeeded())
        {
            return AssetResult<AssetPairBytes>(valid);
        }
        if (!id.valid())
        {
            return AssetResult<AssetPairBytes>(invalid("Texture2D needs a valid asset ID."));
        }
        Texture2DAssetData metadata;
        metadata.usage = static_cast<std::uint32_t>(texture.usage);
        metadata.flip_green = texture.flip_green;
        metadata.width = texture.width;
        metadata.height = texture.height;
        metadata.pixel_format = static_cast<std::uint32_t>(texture.format);
        metadata.mip_count = static_cast<std::uint32_t>(texture.mips.size());
        ValueWriter writer;
        if (!encode_value(writer, metadata).succeeded())
        {
            return AssetResult<AssetPairBytes>(invalid("Texture2D metadata encoding failed."));
        }
        std::vector<std::uint8_t> payload;
        append_u32(payload, mip_format_version);
        append_u32(payload, metadata.mip_count);
        for (const auto& mip : texture.mips)
        {
            append_u32(payload, mip.row_pitch);
            append_u32(payload, mip.slice_pitch);
            append_u32(payload, static_cast<std::uint32_t>(mip.pixels.size()));
            payload.insert(payload.end(), mip.pixels.begin(), mip.pixels.end());
        }
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.Texture2DAssetData";
        index.schema_version = 2u;
        AssetFileLimits limits;
        limits.max_file_bytes = maximum_file;
        return encode_asset_pair(types, std::move(index), writer.bytes(),
                                 {{"texture_mips", 2u, true, std::move(payload)}}, limits);
    }

    AssetResult<Texture2DAsset> read_texture_asset(const FileSystem& files, const VirtualPath& path)
    {
        TypeRegistry types;
        const ReflectionStatus registered = register_texture_asset_types(types);
        if (!registered.succeeded() || !types.freeze().succeeded())
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D schema registration failed."));
        }
        AssetFileLimits limits;
        limits.max_file_bytes = maximum_file;
        const auto pair = read_asset_pair(types, files, path, limits);
        if (!pair.succeeded())
        {
            return AssetResult<Texture2DAsset>(pair.status());
        }
        const auto& description = pair.value().description;
        if (description.index.root_type != "toy3d.Texture2DAssetData" || description.index.schema_version != 2u ||
            !description.has_meta || !description.index.dependencies.empty() || !description.index.subresources.empty())
        {
            return AssetResult<Texture2DAsset>(invalid("Unsupported Texture2D asset root or dependencies."));
        }
        ValueReader reader(description.type_data);
        Texture2DAssetData metadata;
        if (!decode_value(reader, metadata).succeeded() || !reader.at_end() || metadata.width == 0 ||
            metadata.height == 0 || metadata.width > maximum_dimension || metadata.height > maximum_dimension ||
            metadata.pixel_format >= static_cast<std::uint32_t>(PixelFormat::Max) ||
            !texture_usage_matches_format(static_cast<TextureUsage>(metadata.usage),
                                          static_cast<PixelFormat>(metadata.pixel_format)) ||
            (metadata.flip_green && metadata.usage != static_cast<std::uint32_t>(TextureUsage::Normal)) ||
            metadata.mip_count != full_mip_count(metadata.width, metadata.height))
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D metadata is invalid."));
        }
        const AssetSegmentData* mips = nullptr;
        for (const AssetSegmentData& segment : pair.value().meta.segments)
        {
            if (segment.name == "texture_mips" && segment.kind == 2u && segment.required)
            {
                mips = &segment;
            }
            else if (segment.required)
            {
                return AssetResult<Texture2DAsset>(invalid("Unknown required Texture2D segment."));
            }
        }
        if (!mips || mips->bytes.size() > maximum_payload + 256u)
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D mip payload is missing or oversized."));
        }
        const auto& bytes = mips->bytes;
        std::size_t cursor = 0u;
        std::uint32_t version = 0u;
        std::uint32_t count = 0u;
        if (!read_u32(bytes, cursor, bytes.size(), version) || !read_u32(bytes, cursor, bytes.size(), count) ||
            version != mip_format_version || count != metadata.mip_count)
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D mip version or count is invalid."));
        }
        Texture2DAsset candidate;
        candidate.width = metadata.width;
        candidate.height = metadata.height;
        candidate.usage = static_cast<TextureUsage>(metadata.usage);
        candidate.flip_green = metadata.flip_green;
        candidate.format = static_cast<PixelFormat>(metadata.pixel_format);
        candidate.mips.reserve(count);
        for (std::uint32_t level = 0u; level < count; ++level)
        {
            TextureAssetMip mip;
            std::uint32_t length = 0u;
            if (!read_u32(bytes, cursor, bytes.size(), mip.row_pitch) ||
                !read_u32(bytes, cursor, bytes.size(), mip.slice_pitch) ||
                !read_u32(bytes, cursor, bytes.size(), length) || length > bytes.size() - cursor)
            {
                return AssetResult<Texture2DAsset>(invalid("Texture2D mip payload is truncated."));
            }
            mip.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                              bytes.begin() + static_cast<std::ptrdiff_t>(cursor + length));
            cursor += length;
            candidate.mips.push_back(std::move(mip));
        }
        if (cursor != bytes.size() || !validate_texture_asset(candidate).succeeded())
        {
            return AssetResult<Texture2DAsset>(invalid("Texture2D mip payload is invalid."));
        }
        return AssetResult<Texture2DAsset>(std::move(candidate));
    }
} // namespace toy3d
