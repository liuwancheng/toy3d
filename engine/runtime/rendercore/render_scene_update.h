#pragma once

#include "rendercore/geometry/axis_aligned_bounds.h"
#include "rendercore/render_dirty.h"
#include "rendercore/render_id.h"

#include <vector>

namespace toy3d
{
    enum class RenderSceneUpdateOperation
    {
        Add,
        Update,
        Remove
    };

    enum class LightType
    {
        Directional,
        Point,
        Spot
    };

    struct PrimitiveRenderSnapshot
    {
        mat4x4 world_transform{1.0f};
        AxisAlignedBounds world_bounds;
        MeshRenderResourceId mesh_resource_id;
        std::vector<MaterialRenderResourceId> material_resource_ids;
    };

    struct PrimitiveSceneUpdate
    {
        RenderSceneUpdateOperation operation = RenderSceneUpdateOperation::Update;
        RenderDirtyFlags dirty_flags = RenderDirtyFlags::None;
        PrimitiveId primitive_id;
        PrimitiveRenderSnapshot snapshot;
    };

    struct LightRenderSnapshot
    {
        LightType type = LightType::Directional;
        mat4x4 world_transform{1.0f};
        vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 0.0f;
        float inner_angle_degrees = 0.0f;
        float outer_angle_degrees = 0.0f;
        int render_priority = 0;
        bool enabled = true;
    };

    struct LightSceneUpdate
    {
        RenderSceneUpdateOperation operation = RenderSceneUpdateOperation::Update;
        RenderDirtyFlags dirty_flags = RenderDirtyFlags::None;
        LightId light_id;
        LightRenderSnapshot snapshot;
    };

    struct RenderSceneUpdateBatch
    {
        RenderSceneId scene_id;
        std::vector<PrimitiveSceneUpdate> primitive_updates;
        std::vector<LightSceneUpdate> light_updates;

        bool empty() const
        {
            return primitive_updates.empty() && light_updates.empty();
        }
    };
}
