#include "gamescene/world.h"

#include "gamescene/render_resource_update_collector.h"

#include "gamescene/component/light_component.h"
#include "gamescene/component/static_mesh_component.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    namespace
    {
        constexpr RenderDirtyFlags all_render_dirty_flags =
            RenderDirtyFlags::Transform |
            RenderDirtyFlags::State |
            RenderDirtyFlags::DynamicData;

        PrimitiveRenderSnapshot make_primitive_snapshot(
            const StaticMeshComponent& component)
        {
            PrimitiveRenderSnapshot snapshot;
            snapshot.world_transform = component.world_transform();
            snapshot.world_bounds = component.world_bounds();
            if (component.static_mesh() == nullptr)
            {
                return snapshot;
            }

            snapshot.mesh_resource_id = component.static_mesh()->render_resource_id();
            const std::size_t material_count =
                component.static_mesh()->material_slots().size();
            snapshot.material_resource_ids.reserve(material_count);
            for (std::size_t material_slot = 0;
                material_slot < material_count;
                ++material_slot)
            {
                const MaterialInstanceRef material = component.material_for_slot(
                    static_cast<std::uint32_t>(material_slot));
                snapshot.material_resource_ids.push_back(
                    material != nullptr
                        ? material->render_resource_id()
                        : MaterialRenderResourceId{});
            }
            return snapshot;
        }

        LightRenderSnapshot make_light_snapshot(const LightComponent& component)
        {
            LightRenderSnapshot snapshot;
            snapshot.world_transform = component.world_transform();
            snapshot.color = component.color();
            snapshot.intensity = component.intensity();
            snapshot.render_priority = component.render_priority();
            snapshot.enabled = component.enabled();

            if (const auto* spot = dynamic_cast<const SpotLightComponent*>(&component))
            {
                snapshot.type = LightType::Spot;
                snapshot.range = spot->range();
                snapshot.inner_angle_degrees = spot->inner_angle_degrees();
                snapshot.outer_angle_degrees = spot->outer_angle_degrees();
            }
            else if (const auto* point = dynamic_cast<const PointLightComponent*>(&component))
            {
                snapshot.type = LightType::Point;
                snapshot.range = point->range();
            }
            return snapshot;
        }
    }

    World::World() : render_scene_id_(allocate_render_id<RenderSceneId>()) {}

    Actor& World::create_actor()
    {
        auto actor = std::make_unique<Actor>(*this);
        Actor& result = *actor;
        actors_.push_back(std::move(actor));
        return result;
    }

    bool World::destroy_actor(Actor& actor)
    {
        const auto actor_iterator = std::find_if(
            actors_.begin(), actors_.end(),
            [&actor](const std::unique_ptr<Actor>& candidate)
            {
                return candidate.get() == &actor;
            });
        if (actor_iterator == actors_.end())
        {
            return false;
        }

        collect_actor_removals(actor);
        actors_.erase(actor_iterator);
        return true;
    }

    void World::update_transforms()
    {
        for (const std::unique_ptr<Actor>& actor : actors_)
        {
            for (const std::unique_ptr<SceneComponent>& component : actor->components_)
            {
                component->update_world_transform();
            }
        }
    }

    RenderSceneUpdateBatch World::collect_render_scene_updates()
    {
        update_transforms();

        RenderSceneUpdateBatch batch;
        batch.scene_id = render_scene_id_;
        for (PrimitiveId primitive_id : pending_primitive_removals_)
        {
            PrimitiveSceneUpdate update;
            update.operation = RenderSceneUpdateOperation::Remove;
            update.primitive_id = primitive_id;
            batch.primitive_updates.push_back(std::move(update));
        }
        pending_primitive_removals_.clear();

        for (LightId light_id : pending_light_removals_)
        {
            LightSceneUpdate update;
            update.operation = RenderSceneUpdateOperation::Remove;
            update.light_id = light_id;
            batch.light_updates.push_back(std::move(update));
        }
        pending_light_removals_.clear();

        for (const std::unique_ptr<Actor>& actor : actors_)
        {
            for (const std::unique_ptr<SceneComponent>& component : actor->components_)
            {
                if (auto* primitive = dynamic_cast<StaticMeshComponent*>(component.get()))
                {
                    primitive->update_world_bounds();
                    if (!primitive->primitive_id_ && primitive->static_mesh() != nullptr)
                    {
                        primitive->primitive_id_ = allocate_render_id<PrimitiveId>();
                        PrimitiveSceneUpdate update;
                        update.operation = RenderSceneUpdateOperation::Add;
                        update.dirty_flags = all_render_dirty_flags;
                        update.primitive_id = primitive->primitive_id_;
                        update.snapshot = make_primitive_snapshot(*primitive);
                        batch.primitive_updates.push_back(std::move(update));
                        primitive->clear_render_dirty();
                    }
                    else if (primitive->primitive_id_ &&
                        primitive->render_dirty_flags() != RenderDirtyFlags::None)
                    {
                        PrimitiveSceneUpdate update;
                        update.operation = RenderSceneUpdateOperation::Update;
                        update.dirty_flags = primitive->render_dirty_flags();
                        update.primitive_id = primitive->primitive_id_;
                        update.snapshot = make_primitive_snapshot(*primitive);
                        batch.primitive_updates.push_back(std::move(update));
                        primitive->clear_render_dirty();
                    }
                    continue;
                }

                if (auto* light = dynamic_cast<LightComponent*>(component.get()))
                {
                    if (!light->light_id_)
                    {
                        light->light_id_ = allocate_render_id<LightId>();
                        LightSceneUpdate update;
                        update.operation = RenderSceneUpdateOperation::Add;
                        update.dirty_flags = all_render_dirty_flags;
                        update.light_id = light->light_id_;
                        update.snapshot = make_light_snapshot(*light);
                        batch.light_updates.push_back(std::move(update));
                        light->clear_render_dirty();
                    }
                    else if (light->render_dirty_flags() != RenderDirtyFlags::None)
                    {
                        LightSceneUpdate update;
                        update.operation = RenderSceneUpdateOperation::Update;
                        update.dirty_flags = light->render_dirty_flags();
                        update.light_id = light->light_id_;
                        update.snapshot = make_light_snapshot(*light);
                        batch.light_updates.push_back(std::move(update));
                        light->clear_render_dirty();
                    }
                }
            }
        }
        return batch;
    }

    void World::collect_actor_removals(const Actor& actor)
    {
        for (const std::unique_ptr<SceneComponent>& component : actor.components_)
        {
            if (const auto* primitive =
                dynamic_cast<const StaticMeshComponent*>(component.get()))
            {
                if (primitive->primitive_id_)
                {
                    pending_primitive_removals_.push_back(primitive->primitive_id_);
                }
            }
            else if (const auto* light =
                dynamic_cast<const LightComponent*>(component.get()))
            {
                if (light->light_id_)
                {
                    pending_light_removals_.push_back(light->light_id_);
                }
            }
        }
    }

    void World::append_render_resources(
        RenderResourceUpdateCollector& collector) const
    {
        for (const std::unique_ptr<Actor>& actor : actors_)
        {
            for (const std::unique_ptr<SceneComponent>& component : actor->components_)
            {
                const auto* primitive =
                    dynamic_cast<const StaticMeshComponent*>(component.get());
                if (primitive == nullptr || primitive->static_mesh() == nullptr)
                {
                    continue;
                }

                collector.add_mesh(primitive->static_mesh());
                const std::size_t material_count =
                    primitive->static_mesh()->material_slots().size();
                for (std::size_t material_slot = 0;
                    material_slot < material_count;
                    ++material_slot)
                {
                    collector.add_material(primitive->material_for_slot(
                        static_cast<std::uint32_t>(material_slot)));
                }
            }
        }
    }
}
