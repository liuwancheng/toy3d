#pragma once

#include "rendercore/render_scene_update.h"
#include "renderscene/resources/render_resource_cache.h"

#include <cstddef>
#include <vector>

namespace toy3d
{
    enum class PrimitiveRenderResourceState
    {
        Ready,
        MissingMesh,
        InvalidMaterialSlots
    };

    struct PrimitiveRenderResources
    {
        PrimitiveRenderResourceState state =
            PrimitiveRenderResourceState::MissingMesh;
        MeshRenderResourceVersionRef mesh;
        std::vector<MaterialRenderResourceVersionRef> materials;
        std::size_t placeholder_material_count = 0;

        bool is_ready() const
        {
            return state == PrimitiveRenderResourceState::Ready;
        }
    };

    PrimitiveRenderResources resolve_primitive_render_resources(
        const PrimitiveRenderSnapshot& snapshot,
        const RenderResourceCache& cache);
}
