#include "renderscene/resources/primitive_render_resources.h"

namespace toy3d
{
    PrimitiveRenderResources resolve_primitive_render_resources(
        const PrimitiveRenderSnapshot& snapshot,
        const RenderResourceCache& cache)
    {
        PrimitiveRenderResources resources;
        const auto mesh_result = cache.resolve_mesh(snapshot.mesh_resource_id);
        if (mesh_result.state != RenderResourceResolveState::Found ||
            mesh_result.version == nullptr)
        {
            return resources;
        }
        resources.mesh = mesh_result.version;

        resources.materials.reserve(snapshot.material_resource_ids.size());
        for (MaterialRenderResourceId material_id :
            snapshot.material_resource_ids)
        {
            const auto material_result = cache.resolve_material(material_id);
            if (material_result.version == nullptr)
            {
                resources.state =
                    PrimitiveRenderResourceState::InvalidMaterialSlots;
                return resources;
            }
            if (material_result.state == RenderResourceResolveState::Placeholder)
            {
                ++resources.placeholder_material_count;
            }
            resources.materials.push_back(material_result.version);
        }

        for (const StaticMeshSection& section : resources.mesh->sections)
        {
            if (section.material_slot >= resources.materials.size())
            {
                resources.state =
                    PrimitiveRenderResourceState::InvalidMaterialSlots;
                return resources;
            }
        }

        resources.state = PrimitiveRenderResourceState::Ready;
        return resources;
    }
}
