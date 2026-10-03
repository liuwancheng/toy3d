#include "static_mesh_builder.h"
#include "asset_pipeline/mesh_tangents.h"

#include <utility>

namespace toy3d
{
    AssetResult<StaticMeshAssetGeometry> build_static_mesh(const MeshDescription& source)
    {
        const AssetStatus valid = validate_mesh_description(source);
        if (!valid.succeeded())
        {
            return AssetResult<StaticMeshAssetGeometry>(valid);
        }
        StaticMeshAssetGeometry result;
        result.material_slots = source.material_slots;
        result.vertices.reserve(source.corners.size());
        for (const MeshCorner& corner : source.corners)
        {
            result.vertices.push_back({source.positions[corner.vertex], corner.normal, corner.uv0, corner.color});
        }
        result.indices.reserve(source.triangles.size() * 3);
        for (std::uint32_t slot = 0; slot < source.material_slots.size(); ++slot)
        {
            StaticMeshAssetSection section;
            section.first_index = static_cast<std::uint32_t>(result.indices.size());
            section.material_slot = slot;
            for (const MeshTriangle& triangle : source.triangles)
            {
                if (triangle.material_slot != slot)
                {
                    continue;
                }
                result.indices.insert(result.indices.end(), triangle.corners.begin(), triangle.corners.end());
            }
            section.index_count = static_cast<std::uint32_t>(result.indices.size()) - section.first_index;
            if (section.index_count != 0)
            {
                result.sections.push_back(section);
            }
        }
        const AssetStatus built = validate_static_mesh_geometry(result);
        if (!built.succeeded())
        {
            return AssetResult<StaticMeshAssetGeometry>(built);
        }
        auto tangents = build_mesh_tangents(result);
        return tangents.succeeded() ? AssetResult<StaticMeshAssetGeometry>(tangents.value().geometry)
                                    : AssetResult<StaticMeshAssetGeometry>(tangents.status());
    }
} // namespace toy3d
