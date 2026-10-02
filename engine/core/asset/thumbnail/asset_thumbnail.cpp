#include "asset_thumbnail.h"

#include <algorithm>

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            AssetStatus status;
            status.code = AssetErrorCode::InvalidFormat;
            status.message = message;
            return status;
        }

        bool write_source(ValueWriter& writer, const AssetThumbnailSource& source)
        {
            if (!writer.write_uint32(source.preview_version).succeeded())
            {
                return false;
            }
            for (std::uint8_t byte : source.content_hash)
            {
                if (!writer.write_uint8(byte).succeeded())
                {
                    return false;
                }
            }
            return true;
        }

        bool read_source(ValueReader& reader, AssetThumbnailSource& source)
        {
            if (!reader.read_uint32(source.preview_version).succeeded() || source.preview_version != 1)
            {
                return false;
            }
            for (std::uint8_t& byte : source.content_hash)
            {
                if (!reader.read_uint8(byte).succeeded())
                {
                    return false;
                }
            }
            return true;
        }

        AssetResult<AssetThumbnailSource> source_from_payloads(const std::vector<std::uint8_t>& typed,
                                                               const std::vector<std::uint8_t>& geometry)
        {
            ValueLimits limits;
            limits.max_bytes = typed.size() + geometry.size() + 1024u;
            ValueWriter writer(limits);
            if (!writer.write_uint32(1u).succeeded() || !writer.write_utf8("type_data").succeeded() ||
                !writer.write_blob(typed).succeeded() || !writer.write_utf8("render_geometry").succeeded() ||
                !writer.write_blob(geometry).succeeded())
            {
                return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source encoding failed."));
            }
            return AssetResult<AssetThumbnailSource>(AssetThumbnailSource{1u, sha256(writer.bytes())});
        }
    } // namespace

    AssetResult<AssetThumbnailSource> calculate_static_mesh_thumbnail_source(const std::vector<std::uint8_t>& bytes)
    {
        const auto inspected = inspect_asset_bytes(bytes);
        if (!inspected.succeeded())
        {
            return AssetResult<AssetThumbnailSource>(inspected.status());
        }
        if (inspected.value().root_type != "toy3d.StaticMeshAssetData")
        {
            return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source requires a StaticMesh asset."));
        }
        ValueLimits limits;
        limits.max_bytes = bytes.size() + 1024;
        ValueWriter writer(limits);
        bool geometry = false;
        bool typed = false;
        if (!writer.write_uint32(1).succeeded())
        {
            return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source encoding failed."));
        }
        for (const char* name : {"type_data", "render_geometry"})
        {
            for (const AssetSegment& segment : inspected.value().segments)
            {
                if (segment.name != name)
                {
                    continue;
                }
                const std::vector<std::uint8_t> payload(
                    bytes.begin() + static_cast<std::ptrdiff_t>(segment.offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(segment.offset + segment.length));
                if (!writer.write_utf8(segment.name).succeeded() || !writer.write_blob(payload).succeeded())
                {
                    return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source exceeds its encoding limit."));
                }
                if (segment.name == "type_data")
                {
                    typed = true;
                }
                else
                {
                    geometry = true;
                }
            }
        }
        if (!typed || !geometry)
        {
            return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source segments are missing."));
        }
        return AssetResult<AssetThumbnailSource>(AssetThumbnailSource{1, sha256(writer.bytes())});
    }

    AssetResult<AssetThumbnailSource> calculate_static_mesh_thumbnail_source(const AssetPair& pair)
    {
        if (pair.description.index.root_type != "toy3d.StaticMeshAssetData" || !pair.description.has_meta)
        {
            return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source requires a StaticMesh pair."));
        }
        for (const AssetSegmentData& segment : pair.meta.segments)
        {
            if (segment.name == "render_geometry" && segment.kind == 2u && segment.required)
            {
                return source_from_payloads(pair.description.type_data, segment.bytes);
            }
        }
        return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source geometry is missing."));
    }

    AssetResult<AssetSegmentData> encode_thumbnail_source(const AssetThumbnailSource& source)
    {
        ValueWriter writer;
        if (source.preview_version != 1 || !writer.write_uint32(1).succeeded() || !write_source(writer, source))
        {
            return AssetResult<AssetSegmentData>(invalid("Thumbnail source version is invalid."));
        }
        return AssetResult<AssetSegmentData>(AssetSegmentData{"thumbnail_source", 2, false, writer.bytes()});
    }

    AssetResult<AssetThumbnailSource> decode_thumbnail_source(const std::vector<std::uint8_t>& bytes)
    {
        ValueReader reader(bytes);
        std::uint32_t version = 0;
        AssetThumbnailSource source;
        if (!reader.read_uint32(version).succeeded() || version != 1 || !read_source(reader, source) ||
            !reader.at_end())
        {
            return AssetResult<AssetThumbnailSource>(invalid("Thumbnail source data is invalid."));
        }
        return AssetResult<AssetThumbnailSource>(source);
    }

    AssetResult<AssetSegmentData> encode_asset_thumbnail(const AssetThumbnailData& thumbnail)
    {
        if (thumbnail.source.preview_version != 1 || !thumbnail.generator_version || !thumbnail.width ||
            !thumbnail.height || thumbnail.width > thumbnail_max_dimension ||
            thumbnail.height > thumbnail_max_dimension || thumbnail.png.empty() ||
            thumbnail.png.size() > thumbnail_max_bytes)
        {
            return AssetResult<AssetSegmentData>(invalid("Thumbnail dimensions or PNG byte count are invalid."));
        }
        ValueLimits limits;
        limits.max_bytes = thumbnail_max_bytes + 128;
        ValueWriter writer(limits);
        if (!writer.write_uint32(1).succeeded() || !write_source(writer, thumbnail.source) ||
            !writer.write_uint32(thumbnail.generator_version).succeeded() ||
            !writer.write_uint32(thumbnail.width).succeeded() || !writer.write_uint32(thumbnail.height).succeeded() ||
            !writer.write_uint8(1).succeeded() || !writer.write_blob(thumbnail.png).succeeded())
        {
            return AssetResult<AssetSegmentData>(invalid("Thumbnail encoding failed."));
        }
        return AssetResult<AssetSegmentData>(AssetSegmentData{"thumbnail", 2, false, writer.bytes()});
    }

    AssetResult<AssetThumbnailData> decode_asset_thumbnail(const std::vector<std::uint8_t>& bytes)
    {
        ValueLimits limits;
        limits.max_bytes = thumbnail_max_bytes + 128;
        ValueReader reader(bytes, limits);
        std::uint32_t version = 0;
        std::uint8_t encoding = 0;
        AssetThumbnailData candidate;
        if (!reader.read_uint32(version).succeeded() || version != 1 || !read_source(reader, candidate.source) ||
            !reader.read_uint32(candidate.generator_version).succeeded() ||
            !reader.read_uint32(candidate.width).succeeded() || !reader.read_uint32(candidate.height).succeeded() ||
            !reader.read_uint8(encoding).succeeded() || encoding != 1 || !reader.read_blob(candidate.png).succeeded() ||
            !reader.at_end() || !candidate.generator_version || !candidate.width || !candidate.height ||
            candidate.width > thumbnail_max_dimension || candidate.height > thumbnail_max_dimension ||
            candidate.png.empty() || candidate.png.size() > thumbnail_max_bytes)
        {
            return AssetResult<AssetThumbnailData>(invalid("Thumbnail data is invalid."));
        }
        return AssetResult<AssetThumbnailData>(std::move(candidate));
    }

} // namespace toy3d
