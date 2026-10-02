#pragma once

#include "math/transform.h"
#include "asset/asset_identity.h"
#include "rendercore/geometry/static_mesh.h"
#include <cstdint>
#include <vector>

namespace toy3d
{
    enum class PlacementItemId : std::uint32_t
    {
        EmptyActor,
        Cube,
        Plane,
        DirectionalLight,
        PointLight,
        Camera,
        StaticMesh
    };

    struct PlacementItem
    {
        PlacementItemId id = PlacementItemId::EmptyActor;
        const char* name = "";
        const char* category = "";
        float ground_offset = 0.0f;
    };

    struct PlacementRequest
    {
        PlacementItemId item = PlacementItemId::EmptyActor;
        Transform transform;
        std::string actor_type;
        // A CPU prototype survives history; each spawn gets fresh render resources.
        StaticMeshRef static_mesh;
        AssetId asset_id;
    };

    const std::vector<PlacementItem>& placement_catalog();
    const PlacementItem* find_placement_item(PlacementItemId id);
    constexpr const char* ACTOR_TYPE_DRAG_PAYLOAD = "TOY3D_ACTOR_TYPE";
    constexpr const char* PLACEMENT_DRAG_PAYLOAD = "TOY3D_PLACE_ACTOR";
} // namespace toy3d
