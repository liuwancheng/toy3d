#include "gamescene/actor/actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/world/world.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/hit_proxy.h"
#include "rendercore/material/material.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    bool nearly_equal(float left, float right)
    {
        return std::abs(left - right) <= 1.0e-5f;
    }

    bool nearly_equal(double left, double right)
    {
        return std::abs(left - right) <= 1.0e-9;
    }

    struct LifecycleCounts
    {
        int initialize = 0;
        int begin_play = 0;
        int tick = 0;
        int end_play = 0;
        toy3d::EndPlayReason last_end_reason = toy3d::EndPlayReason::WorldEndPlay;
        toy3d::WorldTickContext last_tick;
    };

    class LifecycleComponent final : public toy3d::ActorComponent
    {
      public:
        LifecycleComponent(toy3d::Actor& owner, LifecycleCounts& counts) : ActorComponent(owner), counts_(counts) {}

      protected:
        void on_initialize() override { ++counts_.initialize; }
        void on_begin_play() override { ++counts_.begin_play; }
        void on_end_play(toy3d::EndPlayReason reason) override
        {
            ++counts_.end_play;
            counts_.last_end_reason = reason;
        }

      private:
        LifecycleCounts& counts_;
    };

    class LifecycleActor final : public toy3d::Actor
    {
      public:
        LifecycleActor(toy3d::World& world, LifecycleCounts& counts) : Actor(world), counts_(counts) {}

        void destroy_on_next_tick(toy3d::Actor& actor) { destroy_target_ = &actor; }

        void spawn_on_next_tick(LifecycleCounts& counts) { spawn_counts_ = &counts; }

        LifecycleActor* spawned_actor() const { return spawned_actor_; }

      protected:
        void on_initialize() override { ++counts_.initialize; }
        void on_begin_play() override { ++counts_.begin_play; }
        void tick(const toy3d::WorldTickContext& context) override
        {
            ++counts_.tick;
            counts_.last_tick = context;
            if (destroy_target_ != nullptr)
            {
                world().destroy_actor(*destroy_target_);
                destroy_target_ = nullptr;
            }
            if (spawn_counts_ != nullptr)
            {
                spawned_actor_ = &world().spawn_actor<LifecycleActor>(*spawn_counts_);
                spawned_actor_->set_tick_enabled(true);
                spawn_counts_ = nullptr;
            }
        }
        void on_end_play(toy3d::EndPlayReason reason) override
        {
            ++counts_.end_play;
            counts_.last_end_reason = reason;
        }

      private:
        LifecycleCounts& counts_;
        toy3d::Actor* destroy_target_ = nullptr;
        LifecycleCounts* spawn_counts_ = nullptr;
        LifecycleActor* spawned_actor_ = nullptr;
    };

    class TrackingComponent final : public toy3d::ActorComponent
    {
      public:
        TrackingComponent(toy3d::Actor& owner, int& register_count, int& unregister_count)
            : ActorComponent(owner), register_count_(register_count), unregister_count_(unregister_count)
        {
        }

      protected:
        void on_register() override { ++register_count_; }
        void on_unregister() override { ++unregister_count_; }

      private:
        int& register_count_;
        int& unregister_count_;
    };

    toy3d::StaticMeshRef make_mesh()
    {
        toy3d::MaterialDesc material_desc;
        material_desc.shader_name = "Builtin/Surface/Phong";
        const toy3d::MaterialInstanceRef material =
            toy3d::MaterialInstance::create(toy3d::Material::create(std::move(material_desc)));

        toy3d::StaticMeshDesc mesh_desc;
        mesh_desc.vertices = {{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                              {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                              {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
        mesh_desc.indices = std::vector<std::uint16_t>{0, 1, 2};
        mesh_desc.sections.push_back({0, 3, 0});
        mesh_desc.material_slots.push_back(material);
        return toy3d::StaticMesh::create(std::move(mesh_desc));
    }
} // namespace

int main()
{
    using namespace toy3d;

    World world;
    Actor& parent_actor = world.spawn_actor();
    Actor& child_actor = world.spawn_actor();
    check(world.actor_count() == 2 && world.contains(parent_actor) && parent_actor.is_registered(),
          "World::spawn_actor must own and register every spawned Actor");
    check(parent_actor.actor_id() != 0u && child_actor.actor_id() > parent_actor.actor_id() &&
              world.find_actor_by_id(parent_actor.actor_id()) == &parent_actor &&
              world.find_actor_by_id(0u) == nullptr,
          "Actor IDs must be distinct, resolvable, and reserve zero for background hits");

    SceneComponent& parent = parent_actor.create_component<SceneComponent>();
    SceneComponent& child = child_actor.create_component<SceneComponent>();
    check(parent.is_registered() && child.is_registered(),
          "A component created on a registered Actor must register immediately");
    SceneComponent& sibling = parent_actor.create_component<SceneComponent>();
    check(parent.component_id() != 0u && child.component_id() > parent.component_id() &&
              sibling.component_id() > child.component_id() &&
              parent_actor.find_component_by_id(parent.component_id()) == &parent &&
              parent_actor.find_component_by_id(sibling.component_id()) == &sibling &&
              parent_actor.find_component_by_id(0u) == nullptr &&
              child_actor.find_component_by_id(sibling.component_id()) == nullptr,
          "Component identities must be distinct within the World and resolvable by their Actor");

    HitProxyTable hit_table{{HitProxyTargetKind::MeshSection, parent_actor.actor_id(), parent.component_id(), 0u},
                            {HitProxyTargetKind::MeshSection, parent_actor.actor_id(), sibling.component_id(), 2u}};
    HitProxyTarget hit_target;
    check(resolve_hit_proxy({2u}, hit_table, hit_target) &&
              hit_target.actor_id == parent_actor.actor_id() &&
              hit_target.component_id == sibling.component_id() && hit_target.mesh_section_index == 2u &&
              resolve_hit_proxy({0u}, hit_table, hit_target) && hit_target.kind == HitProxyTargetKind::None &&
              !resolve_hit_proxy({3u}, hit_table, hit_target),
          "HitProxy pixel IDs must resolve through their own submission table, including background and invalid IDs");
    check(parent_actor.set_root_component(&parent) && child_actor.set_root_component(&child),
          "An Actor must accept one of its own SceneComponents as root");
    check(!parent_actor.set_root_component(&child), "An Actor must reject a root component owned by another Actor");

    Transform parent_transform;
    parent_transform.translation = {10.0f, 0.0f, 0.0f};
    Transform child_transform;
    child_transform.translation = {0.0f, 2.0f, 0.0f};
    check(parent.set_local_transform(parent_transform) && child.set_local_transform(child_transform) &&
              child.attach_to(&parent, AttachmentRule::KeepRelative),
          "A same-World component hierarchy must accept valid relative transforms");
    check(nearly_equal(child.world_transform().at(3, 0), 10.0f) && nearly_equal(child.world_transform().at(3, 1), 2.0f),
          "Attachment must update the child world transform without a World scan");

    parent_transform.translation.x = 20.0f;
    check(parent.set_local_transform(parent_transform) && nearly_equal(child.world_transform().at(3, 0), 20.0f),
          "A parent transform change must immediately propagate to descendants");
    check(!parent.attach_to(&child, AttachmentRule::KeepRelative), "SceneComponent attachment must reject cycles");

    const Matrix4 world_before_detach = child.world_transform();
    check(child.attach_to(nullptr, AttachmentRule::KeepWorld) &&
              is_nearly_equal(child.world_transform(), world_before_detach, 1.0e-5f),
          "KeepWorld detach must preserve a representable world transform");

    World other_world;
    SceneComponent& other_component = other_world.spawn_actor().create_component<SceneComponent>();
    check(!child.attach_to(&other_component, AttachmentRule::KeepRelative),
          "SceneComponent attachment must reject cross-World parents");

    int register_count = 0;
    int unregister_count = 0;
    Actor& lifecycle_actor = world.spawn_actor();
    const std::uint32_t destroyed_actor_id = lifecycle_actor.actor_id();
    const std::uint64_t generation_before_destruction = world.scene_generation();
    lifecycle_actor.create_component<TrackingComponent>(register_count, unregister_count);
    check(register_count == 1 && unregister_count == 0, "ActorComponent registration must run exactly once");
    check(world.destroy_actor(lifecycle_actor) && unregister_count == 1,
          "World::destroy_actor must unregister components before destruction");
    Actor& replacement_actor = world.spawn_actor();
    check(world.find_actor_by_id(destroyed_actor_id) == nullptr &&
              replacement_actor.actor_id() > destroyed_actor_id &&
              world.scene_generation() > generation_before_destruction,
          "Destroyed Actors must not reuse an Actor ID or preserve the old scene generation");

    World ticking_world;
    LifecycleCounts ticking_actor_counts;
    LifecycleCounts component_counts;
    LifecycleActor& ticking_actor = ticking_world.spawn_actor<LifecycleActor>(ticking_actor_counts);
    LifecycleComponent& lifecycle_component = ticking_actor.create_component<LifecycleComponent>(component_counts);
    ticking_actor.set_tick_enabled(true);
    check(ticking_world.lifecycle_state() == WorldLifecycleState::Created && !ticking_actor.is_initialized() &&
              !lifecycle_component.is_initialized(),
          "Spawning must register objects without initializing a Created World");
    ticking_world.initialize();
    check(ticking_world.lifecycle_state() == WorldLifecycleState::Initialized && ticking_actor_counts.initialize == 1 &&
              component_counts.initialize == 1,
          "World initialization must initialize Components and Actors exactly once");
    ticking_world.begin_play();
    check(ticking_world.lifecycle_state() == WorldLifecycleState::Playing && ticking_actor_counts.begin_play == 1 &&
              component_counts.begin_play == 1,
          "World begin_play must begin initialized Components and Actors");

    LifecycleCounts late_component_counts;
    LifecycleComponent& late_component = ticking_actor.create_component<LifecycleComponent>(late_component_counts);
    check(late_component.is_registered() && late_component.is_initialized() && late_component.has_begun_play() &&
              late_component_counts.initialize == 1 && late_component_counts.begin_play == 1,
          "A Component created during play must catch up to its Actor lifecycle");

    LifecycleCounts spawned_actor_counts;
    ticking_actor.spawn_on_next_tick(spawned_actor_counts);
    check(ticking_world.tick(0.25) && ticking_actor_counts.tick == 1 && ticking_actor.spawned_actor() != nullptr &&
              spawned_actor_counts.initialize == 1 && spawned_actor_counts.begin_play == 1 &&
              spawned_actor_counts.tick == 0,
          "An Actor spawned during Tick must begin play but wait until the next frame to Tick");
    check(ticking_world.tick(0.5) && ticking_actor_counts.tick == 2 && spawned_actor_counts.tick == 1 &&
              nearly_equal(spawned_actor_counts.last_tick.delta_seconds, 0.5) &&
              nearly_equal(ticking_world.world_time_seconds(), 0.75) && ticking_world.frame_number() == 2,
          "World Tick must provide monotonic timing to explicitly enabled Actors");

    LifecycleCounts destroyed_actor_counts;
    LifecycleActor& destroyed_actor = ticking_world.spawn_actor<LifecycleActor>(destroyed_actor_counts);
    destroyed_actor.set_tick_enabled(true);
    ticking_actor.destroy_on_next_tick(destroyed_actor);
    check(ticking_world.tick(0.125) && destroyed_actor_counts.tick == 0 && destroyed_actor_counts.end_play == 1 &&
              destroyed_actor_counts.last_end_reason == EndPlayReason::Destroyed && ticking_world.actor_count() == 2,
          "Destroy during Tick must defer deletion and skip the pending Actor");

    ticking_world.end_play();
    check(ticking_world.lifecycle_state() == WorldLifecycleState::Initialized && ticking_actor_counts.end_play == 1 &&
              component_counts.end_play == 1 && late_component_counts.end_play == 1 &&
              component_counts.last_end_reason == EndPlayReason::WorldEndPlay,
          "World end_play must end Actors and Components without adding Component Tick");

    StaticMeshActor& mesh_actor = world.spawn_actor<StaticMeshActor>();
    check(mesh_actor.root_component() == &mesh_actor.static_mesh_component() &&
              mesh_actor.static_mesh_component().is_registered(),
          "StaticMeshActor must own a registered StaticMeshComponent root");
    const StaticMeshRef mesh = make_mesh();
    check(mesh != nullptr, "The GameScene test mesh must be valid");
    mesh_actor.static_mesh_component().set_static_mesh(mesh);
    Transform mesh_transform;
    mesh_transform.translation.x = 4.0f;
    mesh_transform.scale = Vector3(2.0f);
    check(mesh_actor.static_mesh_component().set_local_transform(mesh_transform) &&
              nearly_equal(mesh_actor.static_mesh_component().world_bounds().minimum.x, 2.0f) &&
              nearly_equal(mesh_actor.static_mesh_component().world_bounds().maximum.x, 6.0f),
          "Primitive bounds must follow component transforms without World collection");

    CameraComponent& camera = world.spawn_actor().create_component<CameraComponent>();
    check(camera.set_perspective(75.0f, 0.25f, 500.0f) && !camera.set_perspective(180.0f, 0.25f, 500.0f) &&
              nearly_equal(camera.vertical_fov_degrees(), 75.0f),
          "CameraComponent must retain valid camera properties without building ViewportFrame");

    PointLightComponent& point = world.spawn_actor().create_component<PointLightComponent>();
    check(point.set_range(20.0f) && !point.set_range(0.0f) && nearly_equal(point.range(), 20.0f),
          "LightComponent validation must remain in the GameScene domain");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " GameScene test(s) failed.\n";
        return 1;
    }
    std::cout << "GameScene tests passed.\n";
    return 0;
}
