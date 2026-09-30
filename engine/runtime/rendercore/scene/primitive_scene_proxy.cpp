#include "rendercore/scene/primitive_scene_proxy.h"

#include <cstddef>
#include <limits>
#include <utility>

#include "logging/logger.h"

namespace toy3d
{
    PrimitiveSceneProxy::PrimitiveSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                             std::uint32_t actor_id, std::uint32_t component_id, bool cast_shadows,
                                             bool receives_shadows)
        : world_transform_(std::move(world_transform)),
          world_bounds_(std::move(world_bounds)), visible_(visible), actor_id_(actor_id),
          component_id_(component_id)
    {
        cast_shadows_ = cast_shadows;
        receives_shadows_ = receives_shadows;
        object_shader_parameters_.toy_object_to_world = world_transform_;
        object_shader_parameters_.toy_receives_shadows = receives_shadows_ ? 1.0f : 0.0f;
        update_normal_transform();
    }

    void PrimitiveSceneProxy::update_transform(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                               bool cast_shadows, bool receives_shadows)
    {
        const bool object_data_changed = world_transform_ != world_transform ||
            receives_shadows_ != receives_shadows;
        world_transform_ = std::move(world_transform);
        object_shader_parameters_.toy_object_to_world = world_transform_;
        update_normal_transform();
        if (object_data_changed)
        {
            object_data_generation_ = object_data_generation_ == std::numeric_limits<std::uint64_t>::max()
                                          ? 1u
                                          : object_data_generation_ + 1u;
        }
        world_bounds_ = std::move(world_bounds);
        visible_ = visible;
        cast_shadows_ = cast_shadows;
        receives_shadows_ = receives_shadows;
        object_shader_parameters_.toy_receives_shadows = receives_shadows_ ? 1.0f : 0.0f;
    }

    void PrimitiveSceneProxy::update_normal_transform()
    {
        Matrix3 linear;
        for (std::size_t column = 0; column < Matrix3::k_column_count; ++column)
            for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
                linear.at(column, row) = world_transform_.at(column, row);
        Matrix3 inverse;
        normal_transform_valid_ = try_inverse(linear, inverse);
        if (!normal_transform_valid_)
            TOY_LOG_ERROR("Primitive has a singular normal transform; it cannot cast shadows.");
        const Matrix3 normal = normal_transform_valid_ ? transpose(inverse) : linear;
        Matrix4 normal_to_world;
        for (std::size_t column = 0; column < Matrix3::k_column_count; ++column)
            for (std::size_t row = 0; row < Matrix3::k_row_count; ++row)
                normal_to_world.at(column, row) = normal.at(column, row);
        object_shader_parameters_.toy_object_normal_to_world = normal_to_world;
    }
} // namespace toy3d
