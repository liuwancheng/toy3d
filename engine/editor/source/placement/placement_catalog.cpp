#include "placement/placement_catalog.h"

namespace toy3d
{
    const std::vector<PlacementItem>& placement_catalog()
    {
        static const std::vector<PlacementItem> items = {
            {PlacementItemId::EmptyActor, "Empty Actor", "Basic", 0.0f},
            {PlacementItemId::Camera, "Camera", "Basic", 1.5f},
            {PlacementItemId::Cube, "Cube", "Shapes", 0.75f},
            {PlacementItemId::Plane, "Plane", "Shapes", 0.0f},
            {PlacementItemId::DirectionalLight, "Directional Light", "Lights", 2.0f},
            {PlacementItemId::PointLight, "Point Light", "Lights", 2.0f}};
        return items;
    }

    const PlacementItem* find_placement_item(PlacementItemId id)
    {
        for (const PlacementItem& item : placement_catalog())
            if (item.id == id) return &item;
        return nullptr;
    }
}
