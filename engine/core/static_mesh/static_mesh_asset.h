#pragma once

#include "mesh_description/mesh_description.h"
#include "static_mesh/static_mesh_asset_data.h"
#include "static_mesh_reflection.h"

namespace toy3d
{
    struct StaticMeshAssetVertex
    {
        Vector3 position;
        Vector3 normal;
        Vector2 uv0;
        std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    };

    struct StaticMeshAssetSection
    {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::uint32_t material_slot = 0;
    };

    struct StaticMeshAssetGeometry
    {
        std::vector<StaticMeshAssetVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<StaticMeshAssetSection> sections;
        std::vector<std::string> material_slots;
    };

    AssetStatus validate_static_mesh_geometry(const StaticMeshAssetGeometry& geometry);
    AssetResult<std::vector<std::uint8_t>> encode_static_mesh_geometry(const StaticMeshAssetGeometry& geometry);
    AssetResult<StaticMeshAssetGeometry> decode_static_mesh_geometry(const std::vector<std::uint8_t>& bytes);
    AssetResult<std::vector<std::uint8_t>> encode_static_mesh_asset(const AssetId& id,
        const StaticMeshAssetGeometry& geometry, std::vector<AssetSegmentData> editor_segments = {});
    AssetResult<StaticMeshAssetGeometry> read_static_mesh_asset(const FileSystem& files, const VirtualPath& path);
} // namespace toy3d
