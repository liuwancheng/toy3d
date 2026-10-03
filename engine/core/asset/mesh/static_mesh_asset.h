#pragma once

#include "asset/mesh/mesh_description.h"
#include "asset/mesh/static_mesh_asset_data.h"
#include "static_mesh_reflection.h"
#include "asset/asset_pair.h"
#include "math/vector4.h"

namespace toy3d
{
    ReflectionStatus register_static_mesh_asset_types(TypeRegistry& types);
    struct StaticMeshAssetVertex
    {
        Vector3 position;
        Vector3 normal;
        Vector2 uv0;
        std::array<std::uint8_t, 4> color{255, 255, 255, 255};
        Vector4 tangent{1, 0, 0, 1};
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
        // Descriptor-only defaults. Geometry payloads keep their existing wire format.
        std::vector<AssetRef> default_materials;
        bool valid_tangent_frame = false;
    };

    AssetStatus validate_static_mesh_geometry(const StaticMeshAssetGeometry& geometry);
    AssetResult<std::vector<std::uint8_t>> encode_static_mesh_geometry(const StaticMeshAssetGeometry& geometry);
    AssetResult<StaticMeshAssetGeometry> decode_static_mesh_geometry(const std::vector<std::uint8_t>& bytes);
    // Immutable snapshot decode accepts opaque optional outer segments; it does
    // not grant permission to re-save them through a typed, lossy writer.
    AssetResult<StaticMeshAssetGeometry> decode_static_mesh_asset(const std::vector<std::uint8_t>& bytes);
    AssetResult<std::vector<std::uint8_t>> encode_static_mesh_asset(const AssetId& id,
                                                                    const StaticMeshAssetGeometry& geometry,
                                                                    std::vector<AssetSegmentData> editor_segments = {});
    AssetResult<AssetPairBytes> encode_static_mesh_asset_pair(const TypeRegistry& types, const AssetId& id,
                                                              const StaticMeshAssetGeometry& geometry,
                                                              std::vector<AssetSegmentData> optional_segments = {});
    AssetResult<StaticMeshAssetGeometry> read_static_mesh_asset(const FileSystem& files, const VirtualPath& path);
    AssetResult<StaticMeshAssetGeometry> decode_static_mesh_asset_pair(const AssetPair& pair);
} // namespace toy3d
