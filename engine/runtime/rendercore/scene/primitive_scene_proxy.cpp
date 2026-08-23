#include "rendercore/scene/primitive_scene_proxy.h"

#include <utility>

namespace toy3d
{
    PrimitiveSceneProxy::PrimitiveSceneProxy(
        Matrix4 world_transform,
        AxisAlignedBounds world_bounds,
        bool visible)
        : world_transform_(std::move(world_transform)),
          world_bounds_(std::move(world_bounds)),
          visible_(visible)
    {
    }

    void PrimitiveSceneProxy::update_transform(
        Matrix4 world_transform,
        AxisAlignedBounds world_bounds,
        bool visible)
    {
        world_transform_ = std::move(world_transform);
        world_bounds_ = std::move(world_bounds);
        visible_ = visible;
    }
}
