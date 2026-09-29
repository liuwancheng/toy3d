#include "static_mesh/static_mesh_asset.h"

#include <set>
#include <algorithm>
#include <utility>

#include "serialization/math_value_codec.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "render_geometry", {}, message, {}};
        }
        AssetStatus validate_metadata(const StaticMeshAssetData& data)
        {
            if (data.material_slots.empty() || data.vertex_count == 0 || data.index_count == 0 ||
                data.index_count % 3 != 0 || data.geometry_segment != "render_geometry")
                return invalid("invalid static mesh metadata");
            return AssetStatus::success();
        }
    }

    AssetStatus validate_static_mesh_geometry(const StaticMeshAssetGeometry& geometry)
    {
        constexpr std::size_t limit = 1000000;
        if (geometry.vertices.empty() || geometry.indices.empty() || geometry.sections.empty() ||
            geometry.material_slots.empty() || geometry.vertices.size() > limit || geometry.indices.size() > limit ||
            geometry.sections.size() > limit || geometry.material_slots.size() > limit || geometry.indices.size() % 3 != 0)
            return invalid("empty or oversized static mesh");
        std::set<std::string> names;
        for (const std::string& name : geometry.material_slots)
            if (name.empty() || !names.insert(name).second) return invalid("empty or duplicate material slot");
        for (const StaticMeshAssetVertex& vertex : geometry.vertices)
        {
            Vector3 normalized;
            if (!is_finite(vertex.position) || !is_finite(vertex.uv0) || !try_normalize(vertex.normal, normalized))
                return invalid("invalid static mesh vertex");
        }
        for (const std::uint32_t index : geometry.indices)
            if (index >= geometry.vertices.size()) return invalid("out of range static mesh index");
        std::size_t next = 0;
        for (const StaticMeshAssetSection& section : geometry.sections)
        {
            if (section.first_index != next || section.index_count == 0 || section.index_count % 3 != 0 ||
                section.index_count > geometry.indices.size() - next || section.material_slot >= geometry.material_slots.size())
                return invalid("invalid or noncontiguous static mesh sections");
            next += section.index_count;
        }
        if (next != geometry.indices.size()) return invalid("static mesh sections do not cover indices");
        for (std::size_t i = 0; i < geometry.indices.size(); i += 3)
        {
            const Vector3& a = geometry.vertices[geometry.indices[i]].position;
            const Vector3& b = geometry.vertices[geometry.indices[i + 1]].position;
            const Vector3& c = geometry.vertices[geometry.indices[i + 2]].position;
            Vector3 normal;
            if (!try_normalize(cross(b - a, c - a), normal)) return invalid("degenerate static mesh triangle");
        }
        return AssetStatus::success();
    }

    AssetResult<std::vector<std::uint8_t>> encode_static_mesh_geometry(const StaticMeshAssetGeometry& geometry)
    {
        const AssetStatus valid = validate_static_mesh_geometry(geometry);
        if (!valid.succeeded()) return AssetResult<std::vector<std::uint8_t>>(valid);
        ValueWriter writer;
        if (!writer.write_uint32(1).succeeded() ||
            !writer.write_array_length(static_cast<std::uint32_t>(geometry.vertices.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("geometry encoding failed"));
        for (const StaticMeshAssetVertex& vertex : geometry.vertices)
        {
            if (!encode_value(writer, vertex.position).succeeded() || !encode_value(writer, vertex.normal).succeeded() ||
                !encode_value(writer, vertex.uv0).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("vertex encoding failed"));
            for (const std::uint8_t color : vertex.color)
                if (!writer.write_uint8(color).succeeded())
                    return AssetResult<std::vector<std::uint8_t>>(invalid("color encoding failed"));
        }
        if (!writer.write_array_length(static_cast<std::uint32_t>(geometry.indices.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("index count encoding failed"));
        for (const std::uint32_t index : geometry.indices)
            if (!writer.write_uint32(index).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("index encoding failed"));
        if (!writer.write_array_length(static_cast<std::uint32_t>(geometry.sections.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("section count encoding failed"));
        for (const StaticMeshAssetSection& section : geometry.sections)
            if (!writer.write_uint32(section.first_index).succeeded() || !writer.write_uint32(section.index_count).succeeded() ||
                !writer.write_uint32(section.material_slot).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("section encoding failed"));
        if (!writer.write_array_length(static_cast<std::uint32_t>(geometry.material_slots.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("material count encoding failed"));
        for (const std::string& name : geometry.material_slots)
            if (!writer.write_utf8(name).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("material encoding failed"));
        return AssetResult<std::vector<std::uint8_t>>(writer.bytes());
    }

    AssetResult<StaticMeshAssetGeometry> decode_static_mesh_geometry(const std::vector<std::uint8_t>& bytes)
    {
        ValueReader reader(bytes);
        StaticMeshAssetGeometry geometry;
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (!reader.read_uint32(version).succeeded() || version != 1 || !reader.read_array_length(count).succeeded())
            return AssetResult<StaticMeshAssetGeometry>(invalid("invalid geometry version or vertex count"));
        geometry.vertices.resize(count);
        for (StaticMeshAssetVertex& vertex : geometry.vertices)
        {
            if (!decode_value(reader, vertex.position).succeeded() || !decode_value(reader, vertex.normal).succeeded() ||
                !decode_value(reader, vertex.uv0).succeeded())
                return AssetResult<StaticMeshAssetGeometry>(invalid("invalid vertex data"));
            for (std::uint8_t& color : vertex.color)
                if (!reader.read_uint8(color).succeeded())
                    return AssetResult<StaticMeshAssetGeometry>(invalid("invalid vertex color"));
        }
        if (!reader.read_array_length(count).succeeded())
            return AssetResult<StaticMeshAssetGeometry>(invalid("invalid index count"));
        geometry.indices.resize(count);
        for (std::uint32_t& index : geometry.indices)
            if (!reader.read_uint32(index).succeeded())
                return AssetResult<StaticMeshAssetGeometry>(invalid("invalid indices"));
        if (!reader.read_array_length(count).succeeded())
            return AssetResult<StaticMeshAssetGeometry>(invalid("invalid section count"));
        geometry.sections.resize(count);
        for (StaticMeshAssetSection& section : geometry.sections)
            if (!reader.read_uint32(section.first_index).succeeded() || !reader.read_uint32(section.index_count).succeeded() ||
                !reader.read_uint32(section.material_slot).succeeded())
                return AssetResult<StaticMeshAssetGeometry>(invalid("invalid sections"));
        if (!reader.read_array_length(count).succeeded())
            return AssetResult<StaticMeshAssetGeometry>(invalid("invalid material count"));
        geometry.material_slots.resize(count);
        for (std::string& name : geometry.material_slots)
            if (!reader.read_utf8(name).succeeded())
                return AssetResult<StaticMeshAssetGeometry>(invalid("invalid material names"));
        if (!reader.at_end()) return AssetResult<StaticMeshAssetGeometry>(invalid("trailing geometry data"));
        const AssetStatus valid = validate_static_mesh_geometry(geometry);
        if (!valid.succeeded()) return AssetResult<StaticMeshAssetGeometry>(valid);
        return AssetResult<StaticMeshAssetGeometry>(std::move(geometry));
    }

    AssetResult<std::vector<std::uint8_t>> encode_static_mesh_asset(const AssetId& id,
        const StaticMeshAssetGeometry& geometry, std::vector<AssetSegmentData> editor_segments)
    {
        const auto blob = encode_static_mesh_geometry(geometry);
        if (!blob.succeeded()) return AssetResult<std::vector<std::uint8_t>>(blob.status());
        StaticMeshAssetData metadata;
        metadata.material_slots = geometry.material_slots;
        metadata.vertex_count = static_cast<std::uint32_t>(geometry.vertices.size());
        metadata.index_count = static_cast<std::uint32_t>(geometry.indices.size());
        ValueWriter writer;
        const ValueStatus typed = encode_value(writer, metadata);
        if (!typed.succeeded()) return AssetResult<std::vector<std::uint8_t>>(invalid("metadata encoding failed"));
        AssetFileIndex index;
        index.asset_id = id;
        index.root_type = "toy3d.StaticMeshAssetData";
        index.schema_version = 1;
        // Rebuilding render data invalidates all previous preview metadata.
        // The importer computes a new source signature after the new package is encoded.
        editor_segments.erase(std::remove_if(editor_segments.begin(), editor_segments.end(),
            [](const AssetSegmentData& segment)
            { return segment.name == "thumbnail_source" || segment.name == "thumbnail"; }), editor_segments.end());
        editor_segments.push_back({"type_data", 1, true, writer.bytes()});
        editor_segments.push_back({"render_geometry", 2, true, blob.value()});
        return encode_asset_file(std::move(index), std::move(editor_segments));
    }

    AssetResult<StaticMeshAssetGeometry> decode_static_mesh_asset(const std::vector<std::uint8_t>& bytes)
    {
        const auto index = inspect_asset_bytes(bytes);
        if (!index.succeeded()) return AssetResult<StaticMeshAssetGeometry>(index.status());
        if (index.value().root_type != "toy3d.StaticMeshAssetData" || index.value().schema_version != 1)
            return AssetResult<StaticMeshAssetGeometry>(invalid("unsupported static mesh root type or schema"));
        const AssetSegment* typed = nullptr;
        const AssetSegment* render_geometry = nullptr;
        for (const AssetSegment& segment : index.value().segments)
        {
            if (segment.required && segment.name != "type_data" && segment.name != "render_geometry")
                return AssetResult<StaticMeshAssetGeometry>(invalid("unknown required static mesh segment"));
            if (segment.name == "type_data") typed = &segment;
            if (segment.name == "render_geometry") render_geometry = &segment;
        }
        if (!typed || typed->kind != 1 || !typed->required || !render_geometry || render_geometry->kind != 2 ||
            !render_geometry->required || typed->length > ValueLimits{}.max_bytes || render_geometry->length > ValueLimits{}.max_bytes)
            return AssetResult<StaticMeshAssetGeometry>(invalid("invalid or oversized static mesh segment declaration"));
        const std::vector<std::uint8_t> metadata_bytes(bytes.begin() + static_cast<std::ptrdiff_t>(typed->offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(typed->offset + typed->length));
        ValueReader reader(metadata_bytes);
        StaticMeshAssetData metadata;
        const auto decoded = decode_value(reader, metadata);
        if (!decoded.succeeded() || !reader.at_end() || !validate_metadata(metadata).succeeded())
            return AssetResult<StaticMeshAssetGeometry>(invalid("invalid static mesh metadata"));
        const std::vector<std::uint8_t> geometry_bytes(bytes.begin() + static_cast<std::ptrdiff_t>(render_geometry->offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(render_geometry->offset + render_geometry->length));
        const auto geometry = decode_static_mesh_geometry(geometry_bytes);
        if (!geometry.succeeded()) return geometry;
        if (geometry.value().vertices.size() != metadata.vertex_count || geometry.value().indices.size() != metadata.index_count ||
            geometry.value().material_slots != metadata.material_slots)
            return AssetResult<StaticMeshAssetGeometry>(invalid("metadata and geometry disagree"));
        return geometry;
    }

    AssetResult<StaticMeshAssetGeometry> read_static_mesh_asset(const FileSystem& files, const VirtualPath& path)
    {
        const auto bytes = files.read_binary(path, ValueLimits{}.max_bytes);
        if (!bytes.succeeded())
            return AssetResult<StaticMeshAssetGeometry>({AssetErrorCode::Io, {}, path.utf8(), {}, {},
                bytes.status().message, bytes.status()});
        return decode_static_mesh_asset(bytes.value());
    }
} // namespace toy3d
