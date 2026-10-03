#include "rendercore/geometry/static_mesh_asset_loader.h"

#include <utility>

#include "logging/logger.h"

namespace toy3d
{
    StaticMeshRef create_static_mesh_from_asset(const StaticMeshAssetGeometry& geometry,
                                                const MaterialInterfaceRef& default_material,
                                                const MeshMaterialResolver& resolver)
    {
        if (!default_material || !validate_static_mesh_geometry(geometry).succeeded())
        {
            return nullptr;
        }
        StaticMeshDesc desc;
        const auto materials =
            load_mesh_materials(geometry.material_slots, geometry.default_materials, default_material, resolver,
                                shader::VertexFactoryType::Local, geometry.valid_tangent_frame);
        if (!materials.succeeded())
        {
            TOY_LOG_ERROR("StaticMesh default materials rejected: {}", materials.status().message);
            return nullptr;
        }
        desc.valid_tangent_frame = geometry.valid_tangent_frame;
        desc.vertices.reserve(geometry.vertices.size());
        desc.vertex_colors.reserve(geometry.vertices.size());
        for (const StaticMeshAssetVertex& vertex : geometry.vertices)
        {
            // StaticMeshDesc retains legacy GLM storage; convert only at this adapter.
            desc.vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z},
                                     {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                                     {vertex.uv0.x, vertex.uv0.y},
                                     {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, vertex.tangent.w}});
            desc.vertex_colors.push_back(vertex.color);
        }
        // C++17 variant selects the UInt32 asset payload without an untyped buffer.
        desc.indices = geometry.indices;
        for (const StaticMeshAssetSection& section : geometry.sections)
        {
            desc.sections.push_back({section.first_index, section.index_count, section.material_slot});
        }
        desc.material_slots = materials.value();
        desc.default_material_references = geometry.default_materials;
        desc.material_slot_names = geometry.material_slots;
        return StaticMesh::create(std::move(desc));
    }
} // namespace toy3d
