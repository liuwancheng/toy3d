#include "assets/material/material_thumbnail.h"

namespace toy3d
{
    StaticMeshAssetGeometry make_material_preview_geometry(const StaticMeshRef& mesh, bool upright_plane)
    {
        StaticMeshAssetGeometry geometry;
        if (!mesh)
        {
            return geometry;
        }
        for (const auto& vertex : mesh->vertices())
        {
            StaticMeshAssetVertex copy;
            copy.position = Vector3(vertex.position.x, vertex.position.y, vertex.position.z);
            copy.normal = Vector3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            copy.uv0 = Vector2(vertex.uv0.x, vertex.uv0.y);
            copy.tangent = Vector4(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, vertex.tangent.w);
            if (upright_plane)
            {
                // Rotate the placement plane about X: +Y faces -Z, toward the default preview camera.
                copy.position = Vector3(copy.position.x, copy.position.z, -copy.position.y);
                copy.normal = Vector3(copy.normal.x, copy.normal.z, -copy.normal.y);
                copy.tangent = Vector4(copy.tangent.x, copy.tangent.z, -copy.tangent.y, copy.tangent.w);
            }
            geometry.vertices.push_back(copy);
        }
        // C++17 get_if keeps the two supported runtime index widths explicit in the CPU preview copy.
        if (const auto* indices = std::get_if<std::vector<std::uint16_t>>(&mesh->indices()))
        {
            geometry.indices.assign(indices->begin(), indices->end());
        }
        else if (const auto* indices = std::get_if<std::vector<std::uint32_t>>(&mesh->indices()))
        {
            geometry.indices = *indices;
        }
        for (const auto& section : mesh->sections())
        {
            geometry.sections.push_back({section.first_index, section.index_count, section.material_slot});
        }
        geometry.material_slots = mesh->material_slot_names();
        if (geometry.material_slots.empty())
        {
            geometry.material_slots.resize(mesh->material_slots().size(), "Preview");
        }
        geometry.valid_tangent_frame = mesh->has_valid_tangent_frame();
        return geometry;
    }

    AssetStatus prepare_material_thumbnail(
        AssetPreviewScene& preview, const StaticMeshAssetGeometry& sphere, const AssetRef& reference,
        const std::function<AssetResult<MaterialInterfaceRef>(const AssetRef&)>& resolver)
    {
        if (!resolver)
        {
            return {AssetErrorCode::InvalidState, {}, {}, {}, {}, "Material thumbnail resolver is not configured.", {}};
        }
        const auto material = resolver(reference);
        if (!material.succeeded())
        {
            return material.status();
        }
        if (!preview.configure_thumbnail() || !preview.prepare(sphere, material.value()))
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, "Could not prepare the material thumbnail sphere.", {}};
        }
        return AssetStatus::success();
    }
} // namespace toy3d
