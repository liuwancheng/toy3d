#pragma once

#include "math/transform.h"
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
        PointLight
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
    };

    const std::vector<PlacementItem>& placement_catalog();
    const PlacementItem* find_placement_item(PlacementItemId id);
    constexpr const char* PLACEMENT_DRAG_PAYLOAD = "TOY3D_PLACE_ACTOR";
}
