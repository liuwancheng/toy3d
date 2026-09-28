#include "mesh_description/mesh_description.h"

#include <set>
#include <utility>

#include "serialization/math_value_codec.h"

namespace toy3d
{
    namespace
    {
        AssetStatus invalid(const char* message)
        {
            return {AssetErrorCode::Value, {}, {}, "source_mesh", {}, message, {}};
        }
    }

    AssetStatus validate_mesh_description(const MeshDescription& mesh)
    {
        constexpr std::size_t limit = 1000000;
        if (mesh.positions.empty() || mesh.corners.empty() || mesh.triangles.empty() || mesh.material_slots.empty() ||
            mesh.positions.size() > limit || mesh.corners.size() > limit || mesh.triangles.size() > limit / 3 ||
            mesh.material_slots.size() > limit)
            return invalid("empty or oversized source mesh");
        std::set<std::string> names;
        for (const std::string& name : mesh.material_slots)
            if (name.empty() || !names.insert(name).second) return invalid("empty or duplicate material slot");
        for (const Vector3& position : mesh.positions)
            if (!is_finite(position)) return invalid("nonfinite position");
        for (const MeshCorner& corner : mesh.corners)
        {
            Vector3 normalized;
            if (corner.vertex >= mesh.positions.size() || !is_finite(corner.uv0) ||
                !try_normalize(corner.normal, normalized)) return invalid("invalid corner attributes");
        }
        for (const MeshTriangle& triangle : mesh.triangles)
        {
            if (triangle.material_slot >= mesh.material_slots.size()) return invalid("invalid material slot");
            for (const std::uint32_t corner : triangle.corners)
                if (corner >= mesh.corners.size()) return invalid("invalid triangle corner");
            const Vector3& a = mesh.positions[mesh.corners[triangle.corners[0]].vertex];
            const Vector3& b = mesh.positions[mesh.corners[triangle.corners[1]].vertex];
            const Vector3& c = mesh.positions[mesh.corners[triangle.corners[2]].vertex];
            Vector3 normal;
            if (!try_normalize(cross(b - a, c - a), normal)) return invalid("degenerate triangle");
        }
        return AssetStatus::success();
    }

    AssetResult<std::vector<std::uint8_t>> encode_mesh_description(const MeshDescription& mesh)
    {
        const AssetStatus valid = validate_mesh_description(mesh);
        if (!valid.succeeded()) return AssetResult<std::vector<std::uint8_t>>(valid);
        ValueWriter writer;
        if (!writer.write_uint32(1).succeeded() ||
            !writer.write_array_length(static_cast<std::uint32_t>(mesh.positions.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("source encoding exceeded limits"));
        for (const Vector3& position : mesh.positions)
            if (!encode_value(writer, position).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("position encoding failed"));
        if (!writer.write_array_length(static_cast<std::uint32_t>(mesh.corners.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("corner count encoding failed"));
        for (const MeshCorner& corner : mesh.corners)
        {
            if (!writer.write_uint32(corner.vertex).succeeded() || !encode_value(writer, corner.normal).succeeded() ||
                !encode_value(writer, corner.uv0).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("corner encoding failed"));
            for (const std::uint8_t color : corner.color)
                if (!writer.write_uint8(color).succeeded())
                    return AssetResult<std::vector<std::uint8_t>>(invalid("color encoding failed"));
        }
        if (!writer.write_array_length(static_cast<std::uint32_t>(mesh.triangles.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("triangle count encoding failed"));
        for (const MeshTriangle& triangle : mesh.triangles)
        {
            for (const std::uint32_t corner : triangle.corners)
                if (!writer.write_uint32(corner).succeeded())
                    return AssetResult<std::vector<std::uint8_t>>(invalid("triangle encoding failed"));
            if (!writer.write_uint32(triangle.material_slot).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("slot encoding failed"));
        }
        if (!writer.write_array_length(static_cast<std::uint32_t>(mesh.material_slots.size())).succeeded())
            return AssetResult<std::vector<std::uint8_t>>(invalid("slot count encoding failed"));
        for (const std::string& name : mesh.material_slots)
            if (!writer.write_utf8(name).succeeded())
                return AssetResult<std::vector<std::uint8_t>>(invalid("slot name encoding failed"));
        return AssetResult<std::vector<std::uint8_t>>(writer.bytes());
    }

    AssetResult<MeshDescription> decode_mesh_description(const std::vector<std::uint8_t>& bytes)
    {
        ValueReader reader(bytes);
        MeshDescription mesh;
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (!reader.read_uint32(version).succeeded() || version != 1 || !reader.read_array_length(count).succeeded())
            return AssetResult<MeshDescription>(invalid("invalid source version or vertex count"));
        mesh.positions.resize(count);
        for (Vector3& position : mesh.positions)
            if (!decode_value(reader, position).succeeded())
                return AssetResult<MeshDescription>(invalid("invalid source positions"));
        if (!reader.read_array_length(count).succeeded())
            return AssetResult<MeshDescription>(invalid("invalid corner count"));
        mesh.corners.resize(count);
        for (MeshCorner& corner : mesh.corners)
        {
            if (!reader.read_uint32(corner.vertex).succeeded() || !decode_value(reader, corner.normal).succeeded() ||
                !decode_value(reader, corner.uv0).succeeded())
                return AssetResult<MeshDescription>(invalid("invalid corner data"));
            for (std::uint8_t& color : corner.color)
                if (!reader.read_uint8(color).succeeded())
                    return AssetResult<MeshDescription>(invalid("invalid corner color"));
        }
        if (!reader.read_array_length(count).succeeded())
            return AssetResult<MeshDescription>(invalid("invalid triangle count"));
        mesh.triangles.resize(count);
        for (MeshTriangle& triangle : mesh.triangles)
        {
            for (std::uint32_t& corner : triangle.corners)
                if (!reader.read_uint32(corner).succeeded())
                    return AssetResult<MeshDescription>(invalid("invalid triangle indices"));
            if (!reader.read_uint32(triangle.material_slot).succeeded())
                return AssetResult<MeshDescription>(invalid("invalid triangle material"));
        }
        if (!reader.read_array_length(count).succeeded())
            return AssetResult<MeshDescription>(invalid("invalid material count"));
        mesh.material_slots.resize(count);
        for (std::string& name : mesh.material_slots)
            if (!reader.read_utf8(name).succeeded())
                return AssetResult<MeshDescription>(invalid("invalid material name"));
        if (!reader.at_end()) return AssetResult<MeshDescription>(invalid("trailing source data"));
        const AssetStatus valid = validate_mesh_description(mesh);
        if (!valid.succeeded()) return AssetResult<MeshDescription>(valid);
        return AssetResult<MeshDescription>(std::move(mesh));
    }
} // namespace toy3d
