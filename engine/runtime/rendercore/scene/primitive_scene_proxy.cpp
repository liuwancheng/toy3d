#include "rendercore/scene/primitive_scene_proxy.h"

#include <limits>
#include <utility>

namespace toy3d
{
    PrimitiveSceneProxy::PrimitiveSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                             std::uint32_t actor_id, std::uint32_t component_id)
        : world_transform_(std::move(world_transform)), object_shader_parameters_{world_transform_},
          world_bounds_(std::move(world_bounds)), visible_(visible), actor_id_(actor_id),
          component_id_(component_id)
    {
    }

    void PrimitiveSceneProxy::update_transform(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible)
    {
        const bool object_data_changed = world_transform_ != world_transform;
        world_transform_ = std::move(world_transform);
        object_shader_parameters_.toy_object_to_world = world_transform_;
        if (object_data_changed)
        {
            object_data_generation_ = object_data_generation_ == std::numeric_limits<std::uint64_t>::max()
                                          ? 1u
                                          : object_data_generation_ + 1u;
        }
        world_bounds_ = std::move(world_bounds);
        visible_ = visible;
    }
} // namespace toy3d
