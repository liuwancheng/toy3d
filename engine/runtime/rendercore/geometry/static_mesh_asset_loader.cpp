#include "rendercore/geometry/static_mesh_asset_loader.h"

#include <utility>

namespace toy3d
{
    StaticMeshRef create_static_mesh_from_asset(const StaticMeshAssetGeometry& geometry,
                                               const MaterialInterfaceRef& default_material)
    {
        if (!default_material || !validate_static_mesh_geometry(geometry).succeeded()) return nullptr;
        StaticMeshDesc desc;
        desc.vertices.reserve(geometry.vertices.size());
        desc.vertex_colors.reserve(geometry.vertices.size());
        for (const StaticMeshAssetVertex& vertex : geometry.vertices)
        {
            // StaticMeshDesc retains legacy GLM storage; convert only at this adapter.
            desc.vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z},
                                     {vertex.normal.x, vertex.normal.y, vertex.normal.z}, {vertex.uv0.x, vertex.uv0.y}});
            desc.vertex_colors.push_back(vertex.color);
        }
        // C++17 variant selects the UInt32 asset payload without an untyped buffer.
        desc.indices = geometry.indices;
        for (const StaticMeshAssetSection& section : geometry.sections)
            desc.sections.push_back({section.first_index, section.index_count, section.material_slot});
        desc.material_slots.assign(geometry.material_slots.size(), default_material);
        desc.material_slot_names = geometry.material_slots;
        return StaticMesh::create(std::move(desc));
    }
} // namespace toy3d
