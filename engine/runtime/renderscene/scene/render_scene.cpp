#include "renderscene/scene/render_scene.h"

#include <cmath>
#include <unordered_set>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr RenderDirtyFlags all_render_dirty_flags =
            RenderDirtyFlags::Transform |
            RenderDirtyFlags::State |
            RenderDirtyFlags::DynamicData;

        bool is_finite(float value)
        {
            return std::isfinite(value);
        }

        bool is_finite(const vec3& value)
        {
            return is_finite(value.x) && is_finite(value.y) && is_finite(value.z);
        }

        bool is_finite(const mat4x4& value)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!is_finite(value[column][row]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool is_valid(const AxisAlignedBounds& bounds)
        {
            return is_finite(bounds.minimum) && is_finite(bounds.maximum) &&
                bounds.minimum.x <= bounds.maximum.x &&
                bounds.minimum.y <= bounds.maximum.y &&
                bounds.minimum.z <= bounds.maximum.z;
        }

        bool is_valid_transform(const PrimitiveRenderSnapshot& snapshot)
        {
            return is_finite(snapshot.world_transform) && is_valid(snapshot.world_bounds);
        }

        bool is_valid_state(const PrimitiveRenderSnapshot& snapshot)
        {
            return static_cast<bool>(snapshot.mesh_resource_id);
        }

        bool is_valid_transform(const LightRenderSnapshot& snapshot)
        {
            return is_finite(snapshot.world_transform);
        }

        bool is_valid_dynamic_data(const LightRenderSnapshot& snapshot)
        {
            if (!is_finite(snapshot.color) ||
                snapshot.color.x < 0.0f ||
                snapshot.color.y < 0.0f ||
                snapshot.color.z < 0.0f ||
                !is_finite(snapshot.intensity) ||
                snapshot.intensity < 0.0f)
            {
                return false;
            }

            switch (snapshot.type)
            {
            case LightType::Directional:
                return true;
            case LightType::Point:
                return is_finite(snapshot.range) && snapshot.range > 0.0f;
            case LightType::Spot:
                return is_finite(snapshot.range) && snapshot.range > 0.0f &&
                    is_finite(snapshot.inner_angle_degrees) &&
                    is_finite(snapshot.outer_angle_degrees) &&
                    snapshot.inner_angle_degrees >= 0.0f &&
                    snapshot.inner_angle_degrees <= snapshot.outer_angle_degrees &&
                    snapshot.outer_angle_degrees < 90.0f;
            }
            return false;
        }

        bool contains_only_known_dirty_flags(RenderDirtyFlags flags)
        {
            const std::uint8_t flag_bits = static_cast<std::uint8_t>(flags);
            const std::uint8_t known_bits = static_cast<std::uint8_t>(all_render_dirty_flags);
            return (flag_bits & static_cast<std::uint8_t>(~known_bits)) == 0;
        }

        bool validate_dirty_flags(
            RenderSceneUpdateOperation operation,
            RenderDirtyFlags flags)
        {
            if (!contains_only_known_dirty_flags(flags))
            {
                return false;
            }
            switch (operation)
            {
            case RenderSceneUpdateOperation::Add:
                return flags == all_render_dirty_flags;
            case RenderSceneUpdateOperation::Update:
                return flags != RenderDirtyFlags::None;
            case RenderSceneUpdateOperation::Remove:
                return flags == RenderDirtyFlags::None;
            }
            return false;
        }

        void reject_update(
            RenderSceneApplyResult& result,
            RenderSceneApplyError error,
            RenderSceneObjectType object_type,
            RenderSceneUpdateOperation operation,
            std::uint64_t object_id,
            std::string message)
        {
            ++result.rejected_count;
            result.diagnostics.push_back(
                {error, object_type, operation, object_id, std::move(message)});
        }
    }

    PrimitiveSceneProxy::PrimitiveSceneProxy(PrimitiveRenderSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    void PrimitiveSceneProxy::replace_snapshot(PrimitiveRenderSnapshot snapshot)
    {
        snapshot_ = std::move(snapshot);
    }

    void PrimitiveSceneProxy::update_transform(
        const mat4x4& world_transform,
        const AxisAlignedBounds& world_bounds)
    {
        snapshot_.world_transform = world_transform;
        snapshot_.world_bounds = world_bounds;
    }

    PrimitiveSceneInfo::PrimitiveSceneInfo(
        PrimitiveId primitive_id,
        PrimitiveRenderSnapshot snapshot)
        : primitive_id_(primitive_id),
          proxy_(std::move(snapshot))
    {
    }

    void PrimitiveSceneInfo::replace_proxy(PrimitiveRenderSnapshot snapshot)
    {
        proxy_.replace_snapshot(std::move(snapshot));
    }

    void PrimitiveSceneInfo::update_transform(
        const mat4x4& world_transform,
        const AxisAlignedBounds& world_bounds)
    {
        proxy_.update_transform(world_transform, world_bounds);
    }

    LightSceneProxy::LightSceneProxy(LightRenderSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    void LightSceneProxy::replace_snapshot(LightRenderSnapshot snapshot)
    {
        snapshot_ = std::move(snapshot);
    }

    void LightSceneProxy::update_transform(const mat4x4& world_transform)
    {
        snapshot_.world_transform = world_transform;
    }

    void LightSceneProxy::update_dynamic_data(const LightRenderSnapshot& snapshot)
    {
        snapshot_.color = snapshot.color;
        snapshot_.intensity = snapshot.intensity;
        snapshot_.range = snapshot.range;
        snapshot_.inner_angle_degrees = snapshot.inner_angle_degrees;
        snapshot_.outer_angle_degrees = snapshot.outer_angle_degrees;
        snapshot_.render_priority = snapshot.render_priority;
    }

    LightSceneInfo::LightSceneInfo(LightId light_id, LightRenderSnapshot snapshot)
        : light_id_(light_id),
          proxy_(std::move(snapshot))
    {
    }

    void LightSceneInfo::replace_proxy(LightRenderSnapshot snapshot)
    {
        proxy_.replace_snapshot(std::move(snapshot));
    }

    void LightSceneInfo::update_transform(const mat4x4& world_transform)
    {
        proxy_.update_transform(world_transform);
    }

    void LightSceneInfo::update_dynamic_data(const LightRenderSnapshot& snapshot)
    {
        proxy_.update_dynamic_data(snapshot);
    }

    RenderScene::RenderScene(RenderSceneId scene_id) : scene_id_(scene_id)
    {
    }

    const PrimitiveSceneInfo* RenderScene::find_primitive(PrimitiveId primitive_id) const
    {
        const auto iterator = primitives_.find(primitive_id.value());
        return iterator != primitives_.end() ? &iterator->second : nullptr;
    }

    const LightSceneInfo* RenderScene::find_light(LightId light_id) const
    {
        const auto iterator = lights_.find(light_id.value());
        return iterator != lights_.end() ? &iterator->second : nullptr;
    }

    RenderSceneApplyResult RenderScene::apply_updates(const RenderSceneUpdateBatch& batch)
    {
        RenderSceneApplyResult result;
        if (!scene_id_ || batch.scene_id != scene_id_)
        {
            result.rejected_count =
                batch.primitive_updates.size() + batch.light_updates.size();
            result.diagnostics.push_back(
                {RenderSceneApplyError::InvalidScene,
                    RenderSceneObjectType::Scene,
                    RenderSceneUpdateOperation::Update,
                    batch.scene_id.value(),
                    "The RenderScene update batch does not target this scene."});
            return result;
        }

        std::unordered_set<std::uint64_t> seen_primitives;
        for (const PrimitiveSceneUpdate& update : batch.primitive_updates)
        {
            const std::uint64_t id = update.primitive_id.value();
            if (!update.primitive_id)
            {
                reject_update(result, RenderSceneApplyError::InvalidObjectId,
                    RenderSceneObjectType::Primitive, update.operation, id,
                    "A Primitive update must have a valid PrimitiveId.");
                continue;
            }
            if (!seen_primitives.insert(id).second)
            {
                reject_update(result, RenderSceneApplyError::DuplicateUpdateInBatch,
                    RenderSceneObjectType::Primitive, update.operation, id,
                    "A RenderScene update batch may mention a PrimitiveId only once.");
                continue;
            }
            if (!validate_dirty_flags(update.operation, update.dirty_flags))
            {
                reject_update(result, RenderSceneApplyError::InvalidDirtyFlags,
                    RenderSceneObjectType::Primitive, update.operation, id,
                    "The Primitive update has dirty flags incompatible with its operation.");
                continue;
            }

            auto iterator = primitives_.find(id);
            if (update.operation == RenderSceneUpdateOperation::Add)
            {
                if (iterator != primitives_.end())
                {
                    reject_update(result, RenderSceneApplyError::DuplicateAdd,
                        RenderSceneObjectType::Primitive, update.operation, id,
                        "A Primitive Add cannot replace an existing PrimitiveSceneInfo.");
                    continue;
                }
                if (!is_valid_transform(update.snapshot) || !is_valid_state(update.snapshot))
                {
                    reject_update(result, RenderSceneApplyError::InvalidSnapshot,
                        RenderSceneObjectType::Primitive, update.operation, id,
                        "A Primitive Add requires a finite transform, valid bounds and mesh identity.");
                    continue;
                }
                primitives_.emplace(id,
                    PrimitiveSceneInfo(update.primitive_id, update.snapshot));
                ++result.added_count;
                continue;
            }
            if (iterator == primitives_.end())
            {
                const RenderSceneApplyError error =
                    update.operation == RenderSceneUpdateOperation::Update
                        ? RenderSceneApplyError::UnknownUpdate
                        : RenderSceneApplyError::UnknownRemove;
                reject_update(result, error, RenderSceneObjectType::Primitive,
                    update.operation, id,
                    "The Primitive update targets an unknown PrimitiveId.");
                continue;
            }
            if (update.operation == RenderSceneUpdateOperation::Remove)
            {
                primitives_.erase(iterator);
                ++result.removed_count;
                continue;
            }
            if (has_render_dirty_flag(update.dirty_flags, RenderDirtyFlags::DynamicData))
            {
                reject_update(result, RenderSceneApplyError::InvalidDirtyFlags,
                    RenderSceneObjectType::Primitive, update.operation, id,
                    "Primitive DynamicData has no FND-5A payload contract.");
                continue;
            }
            if (has_render_dirty_flag(update.dirty_flags, RenderDirtyFlags::State))
            {
                if (!is_valid_transform(update.snapshot) || !is_valid_state(update.snapshot))
                {
                    reject_update(result, RenderSceneApplyError::InvalidSnapshot,
                        RenderSceneObjectType::Primitive, update.operation, id,
                        "A Primitive State update requires a complete valid snapshot.");
                    continue;
                }
                iterator->second.replace_proxy(update.snapshot);
            }
            else
            {
                if (!is_valid_transform(update.snapshot))
                {
                    reject_update(result, RenderSceneApplyError::InvalidSnapshot,
                        RenderSceneObjectType::Primitive, update.operation, id,
                        "A Primitive Transform update requires finite transform and bounds values.");
                    continue;
                }
                iterator->second.update_transform(
                    update.snapshot.world_transform,
                    update.snapshot.world_bounds);
            }
            ++result.updated_count;
        }

        std::unordered_set<std::uint64_t> seen_lights;
        for (const LightSceneUpdate& update : batch.light_updates)
        {
            const std::uint64_t id = update.light_id.value();
            if (!update.light_id)
            {
                reject_update(result, RenderSceneApplyError::InvalidObjectId,
                    RenderSceneObjectType::Light, update.operation, id,
                    "A Light update must have a valid LightId.");
                continue;
            }
            if (!seen_lights.insert(id).second)
            {
                reject_update(result, RenderSceneApplyError::DuplicateUpdateInBatch,
                    RenderSceneObjectType::Light, update.operation, id,
                    "A RenderScene update batch may mention a LightId only once.");
                continue;
            }
            if (!validate_dirty_flags(update.operation, update.dirty_flags))
            {
                reject_update(result, RenderSceneApplyError::InvalidDirtyFlags,
                    RenderSceneObjectType::Light, update.operation, id,
                    "The Light update has dirty flags incompatible with its operation.");
                continue;
            }

            auto iterator = lights_.find(id);
            if (update.operation == RenderSceneUpdateOperation::Add)
            {
                if (iterator != lights_.end())
                {
                    reject_update(result, RenderSceneApplyError::DuplicateAdd,
                        RenderSceneObjectType::Light, update.operation, id,
                        "A Light Add cannot replace an existing LightSceneInfo.");
                    continue;
                }
                if (!is_valid_transform(update.snapshot) ||
                    !is_valid_dynamic_data(update.snapshot))
                {
                    reject_update(result, RenderSceneApplyError::InvalidSnapshot,
                        RenderSceneObjectType::Light, update.operation, id,
                        "A Light Add requires a complete valid Light snapshot.");
                    continue;
                }
                lights_.emplace(id, LightSceneInfo(update.light_id, update.snapshot));
                ++result.added_count;
                continue;
            }
            if (iterator == lights_.end())
            {
                const RenderSceneApplyError error =
                    update.operation == RenderSceneUpdateOperation::Update
                        ? RenderSceneApplyError::UnknownUpdate
                        : RenderSceneApplyError::UnknownRemove;
                reject_update(result, error, RenderSceneObjectType::Light,
                    update.operation, id,
                    "The Light update targets an unknown LightId.");
                continue;
            }
            if (update.operation == RenderSceneUpdateOperation::Remove)
            {
                lights_.erase(iterator);
                ++result.removed_count;
                continue;
            }

            const bool updates_state =
                has_render_dirty_flag(update.dirty_flags, RenderDirtyFlags::State);
            const bool updates_transform =
                has_render_dirty_flag(update.dirty_flags, RenderDirtyFlags::Transform);
            const bool updates_dynamic_data =
                has_render_dirty_flag(update.dirty_flags, RenderDirtyFlags::DynamicData);
            if ((updates_state || updates_transform) &&
                !is_valid_transform(update.snapshot))
            {
                reject_update(result, RenderSceneApplyError::InvalidSnapshot,
                    RenderSceneObjectType::Light, update.operation, id,
                    "A Light transform or state update requires a finite transform.");
                continue;
            }
            LightRenderSnapshot dynamic_candidate = update.snapshot;
            if (updates_dynamic_data && !updates_state)
            {
                dynamic_candidate = iterator->second.proxy().snapshot();
                dynamic_candidate.color = update.snapshot.color;
                dynamic_candidate.intensity = update.snapshot.intensity;
                dynamic_candidate.range = update.snapshot.range;
                dynamic_candidate.inner_angle_degrees =
                    update.snapshot.inner_angle_degrees;
                dynamic_candidate.outer_angle_degrees =
                    update.snapshot.outer_angle_degrees;
                dynamic_candidate.render_priority = update.snapshot.render_priority;
            }
            if ((updates_state || updates_dynamic_data) &&
                !is_valid_dynamic_data(dynamic_candidate))
            {
                reject_update(result, RenderSceneApplyError::InvalidSnapshot,
                    RenderSceneObjectType::Light, update.operation, id,
                    "A Light dynamic or state update requires valid light parameters.");
                continue;
            }
            if (updates_state)
            {
                iterator->second.replace_proxy(update.snapshot);
            }
            else
            {
                if (updates_transform)
                {
                    iterator->second.update_transform(update.snapshot.world_transform);
                }
                if (updates_dynamic_data)
                {
                    iterator->second.update_dynamic_data(update.snapshot);
                }
            }
            ++result.updated_count;
        }
        return result;
    }
}
