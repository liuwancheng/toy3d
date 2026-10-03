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
#include <functional>
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
        int unregister = 0;
        toy3d::EndPlayReason last_end_reason = toy3d::EndPlayReason::WorldEndPlay;
        toy3d::WorldTickContext last_tick;
    };

    class LifecycleComponent final : public toy3d::ActorComponent
    {
      public:
        LifecycleComponent(toy3d::Actor& owner, LifecycleCounts& counts) : ActorComponent(owner), counts_(counts)
        {
        }

      protected:
        void on_initialize() override
        {
            ++counts_.initialize;
        }
        void on_begin_play() override
        {
            ++counts_.begin_play;
        }
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
        LifecycleActor(toy3d::World& world, LifecycleCounts& counts) : Actor(world), counts_(counts)
        {
        }

        void destroy_on_next_tick(toy3d::Actor& actor)
        {
            destroy_target_ = &actor;
        }

        void spawn_on_next_tick(LifecycleCounts& counts)
        {
            spawn_counts_ = &counts;
        }

        LifecycleActor* spawned_actor() const
        {
            return spawned_actor_;
        }

      protected:
        void on_initialize() override
        {
            ++counts_.initialize;
        }
        void on_begin_play() override
        {
            ++counts_.begin_play;
        }
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
        void on_register() override
        {
            ++register_count_;
        }
        void on_unregister() override
        {
            ++unregister_count_;
        }

      private:
        int& register_count_;
        int& unregister_count_;
    };

    // --------------------------------------------------------------------------
    // ComponentTickProbe: observes generic dispatch and mutates registration during callbacks.
    // --------------------------------------------------------------------------
    class ComponentTickProbe final : public toy3d::ActorComponent
    {
      public:
        ComponentTickProbe(toy3d::Actor& owner, LifecycleCounts& counts, std::vector<int>& events, int label,
                           bool enabled = false, std::function<void()> lifecycle_action = {})
            : ActorComponent(owner), counts_(counts), events_(events), label_(label),
              lifecycle_action_(std::move(lifecycle_action))
        {
            set_tick_enabled(enabled);
        }
        std::function<void()> action;
        std::function<void()> end_action;
        std::function<void()> unregister_action;
        bool succeeds = true;
        bool enable_during_unregister = false;

      protected:
        void on_register() override
        {
            if (lifecycle_action_)
            {
                lifecycle_action_();
            }
        }
        void on_initialize() override
        {
            ++counts_.initialize;
            if (lifecycle_action_)
            {
                lifecycle_action_();
            }
        }
        void on_begin_play() override
        {
            ++counts_.begin_play;
            if (lifecycle_action_)
            {
                lifecycle_action_();
            }
        }
        bool tick_component(const toy3d::WorldTickContext& context) override
        {
            ++counts_.tick;
            counts_.last_tick = context;
            events_.push_back(label_);
            if (action)
            {
                action();
            }
            return succeeds;
        }
        void on_end_play(toy3d::EndPlayReason reason) override
        {
            ++counts_.end_play;
            counts_.last_end_reason = reason;
            if (end_action)
            {
                end_action();
            }
        }
        void on_unregister() override
        {
            ++counts_.unregister;
            if (enable_during_unregister)
            {
                set_tick_enabled(false);
                set_tick_enabled(true);
            }
            if (unregister_action)
            {
                unregister_action();
            }
        }

      private:
        LifecycleCounts& counts_;
        std::vector<int>& events_;
        int label_ = 0;
        std::function<void()> lifecycle_action_;
    };

    // --------------------------------------------------------------------------
    // ActorTickProbe: verifies that every gameplay callback precedes component dispatch.
    // --------------------------------------------------------------------------
    class ActorTickProbe final : public toy3d::Actor
    {
      public:
        ActorTickProbe(toy3d::World& world, std::vector<int>& events, int label,
                       std::function<void()> lifecycle_action = {})
            : Actor(world), events_(events), label_(label), lifecycle_action_(std::move(lifecycle_action))
        {
        }
        std::function<void()> action;

      protected:
        void on_initialize() override
        {
            if (lifecycle_action_)
            {
                lifecycle_action_();
            }
        }
        void on_begin_play() override
        {
            if (lifecycle_action_)
            {
                lifecycle_action_();
            }
        }
        void tick(const toy3d::WorldTickContext&) override
        {
            events_.push_back(label_);
            if (action)
            {
                action();
            }
        }

      private:
        std::vector<int>& events_;
        int label_ = 0;
        std::function<void()> lifecycle_action_;
    };

    void test_component_ticks()
    {
        using namespace toy3d;
        std::vector<int> events;
        LifecycleCounts a_counts, b_counts, disabled_counts, late_counts, spawned_counts;
        World world;
        auto& actor_a = world.spawn_actor<ActorTickProbe>(events, 1);
        auto& actor_b = world.spawn_actor<ActorTickProbe>(events, 2);
        auto& a = actor_a.create_component<ComponentTickProbe>(a_counts, events, 10, true);
        auto& disabled = actor_a.create_component<ComponentTickProbe>(disabled_counts, events, 11);
        auto& b = actor_b.create_component<ComponentTickProbe>(b_counts, events, 20, true);
        actor_a.set_tick_enabled(true);
        actor_b.set_tick_enabled(true);
        check(!disabled.is_tick_enabled() && !world.tick(0.1) && events.empty(),
              "Generic component Tick defaults off and cannot run before play");
        world.begin_play();
        const auto content = world.content_revision();
        actor_a.action = [&]()
        {
            disabled.set_tick_enabled(true);
        };
        check(world.tick(0.1) && events == std::vector<int>({1, 2, 10, 20}) && disabled_counts.tick == 0,
              "All Actor ticks precede components; enabling outside the snapshot waits one frame");
        check(world.content_revision() == content, "Component tick enable and dispatch do not dirty content");
        actor_a.action = {};
        events.clear();
        a.succeeds = false;
        check(!world.tick(0.1) && events == std::vector<int>({1, 2, 10, 20, 11}) &&
                  nearly_equal(world.world_time_seconds(), 0.2) && world.frame_number() == 2 &&
                  b_counts.last_tick.frame_number == 2,
              "Component failure is aggregated while other components and World time continue");
        a.succeeds = true;
        actor_a.set_tick_enabled(false);
        actor_b.set_tick_enabled(false);
        events.clear();
        check(world.tick(0.1) && events == std::vector<int>({10, 20, 11}),
              "Disabling Actor tick does not disable registered component ticks");

        bool spawned = false;
        bool recursive_rejected = false;
        a.action = [&]()
        {
            b.set_tick_enabled(false);
            recursive_rejected = !world.tick(1);
            if (!spawned)
            {
                actor_a.create_component<ComponentTickProbe>(late_counts, events, 30, true);
                auto& late_actor = world.spawn_actor<ActorTickProbe>(events, 99);
                late_actor.set_tick_enabled(true);
                late_actor.create_component<ComponentTickProbe>(spawned_counts, events, 40, true);
                spawned = true;
            }
        };
        events.clear();
        check(world.tick(0.1) && events == std::vector<int>({10, 11}) && recursive_rejected && late_counts.tick == 0 &&
                  spawned_counts.tick == 0 && world.frame_number() == 4,
              "Snapshot survives registration changes; disable is immediate and recursive Tick cannot advance time");
        a.action = {};
        events.clear();
        check(world.tick(0.1) && events == std::vector<int>({99, 10, 11, 30, 40}) && late_counts.tick == 1 &&
                  spawned_counts.tick == 1,
              "Components and Actors created during dispatch first tick next frame");

        b.set_tick_enabled(true);
        b.enable_during_unregister = true;
        const auto victim_id = actor_b.actor_id();
        const auto previous_b_ticks = b_counts.tick;
        a.action = [&]()
        {
            world.destroy_actor(actor_b);
        };
        check(world.tick(0.1) && !world.find_actor_by_id(victim_id) && b_counts.tick == previous_b_ticks,
              "Destroying an owner skips its later component ticks and withdraws registration even if hooks re-enable");
        a.action = {};
        const auto a_ticks = a_counts.tick;
        world.end_play();
        check(!world.tick(0.1) && a_counts.tick == a_ticks, "End play withdraws automatic component execution");
        world.begin_play();
        check(world.tick(0.1) && a_counts.tick == a_ticks + 1 && b_counts.tick == previous_b_ticks,
              "Restart does not duplicate surviving registration or retain destroyed component pointers");

        LifecycleCounts doomed_counts;
        auto& doomed = world.spawn_actor();
        doomed.create_component<ComponentTickProbe>(doomed_counts, events, 50, true);
        const auto doomed_id = doomed.actor_id();
        a.action = [&]()
        {
            world.destroy_actor(doomed);
            world.end_play();
            check(world.actor_count() == 3, "End play during tick must not free the active snapshot");
        };
        check(world.tick(0.1) && !world.find_actor_by_id(doomed_id) && doomed_counts.tick == 0 && !world.is_ticking(),
              "End play inside a component callback safely skips and defers destruction until both phases finish");
        a.action = {};
    }

    void test_lifecycle_tick_guards()
    {
        using namespace toy3d;
        std::vector<int> events;
        LifecycleCounts survivor_counts, victim_counts, replacement_counts;
        World world;
        auto& survivor = world.spawn_actor();
        survivor.create_component<ComponentTickProbe>(survivor_counts, events, 1, true);
        world.begin_play();
        int actor_hooks = 0;
        auto& victim = world.spawn_actor<ActorTickProbe>(events, 2,
                                                         [&]()
                                                         {
                                                             ++actor_hooks;
                                                             check(!world.tick(1),
                                                                   "Direct Actor spawn hooks cannot enter World tick");
                                                         });
        int component_hooks = 0;
        auto& component = victim.create_component<ComponentTickProbe>(
            victim_counts, events, 20, true,
            [&]()
            {
                ++component_hooks;
                check(!world.tick(1), "Direct component creation hooks cannot enter World tick");
            });
        check(actor_hooks == 2 && component_hooks == 3 && world.frame_number() == 0 &&
                  world.world_time_seconds() == 0 && events.empty(),
              "Spawn and component registration/initialization/begin hooks cannot advance any gameplay");
        bool end_called = false;
        bool unregister_called = false;
        component.end_action = [&]()
        {
            end_called = true;
            check(!world.tick(1), "Destroy end_play hook cannot enter World tick");
            check(world.destroy_actor(survivor) && world.actor_count() == 2,
                  "Destroy inside an end hook defers earlier Actor removal until the outer lifecycle completes");
        };
        component.unregister_action = [&]()
        {
            unregister_called = true;
            check(!world.tick(1), "Unregister hook cannot enter World tick");
            // Appending reallocates the original two-Actor vector. Destruction must
            // recover its iterator, and finish every pending removal after callbacks.
            auto& replacement = world.spawn_actor();
            replacement.create_component<ComponentTickProbe>(replacement_counts, events, 30, true);
            check(world.actor_count() == 3, "Unregister hook may create a new owned Actor safely");
        };
        check(world.destroy_actor(victim) && end_called && unregister_called && world.actor_count() == 1 &&
                  survivor_counts.end_play == 1 && victim_counts.end_play == 1 && world.frame_number() == 0,
              "Destroy hooks retain nested guards, handle reallocation and drain all pending owners safely");
        check(world.tick(0.1) && replacement_counts.tick == 1 && survivor_counts.tick == 0 && victim_counts.tick == 0,
              "Lifecycle guards restore dispatch and leave only the replacement component registered");

        LifecycleCounts teardown_counts, appended_counts;
        bool appended = false;
        {
            World exiting;
            auto& actor = exiting.spawn_actor();
            auto& departing = actor.create_component<ComponentTickProbe>(teardown_counts, events, 60, true);
            exiting.begin_play();
            departing.unregister_action = [&]()
            {
                check(!exiting.tick(1), "World teardown unregister hook cannot enter Tick");
                if (!appended)
                {
                    appended = true;
                    exiting.spawn_actor().create_component<ComponentTickProbe>(appended_counts, events, 70, true);
                }
            };
        }
        check(appended && teardown_counts.unregister == 1 && appended_counts.initialize == 1 &&
                  appended_counts.unregister == 1 && appended_counts.tick == 0,
              "World teardown safely drains finite Actor additions without losing the component registry lifetime");
    }

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
    test_component_ticks();
    test_lifecycle_tick_guards();

    World world;
    SceneEnvironmentSettings environment_settings;
    environment_settings.rotation = Quaternion(0.0f, 0.0f, 0.0f, 2.0f);
    environment_settings.intensity = 3.0f;
    check(world.set_environment(environment_settings, nullptr) &&
              world.environment_settings().rotation == Quaternion{} && world.environment_settings().intensity == 3.0f,
          "World must normalize and own its Environment settings");
    const auto environment_revision = world.content_revision();
    environment_settings.intensity = -1.0f;
    check(!world.set_environment(environment_settings, nullptr) && world.content_revision() == environment_revision &&
              world.environment_settings().intensity == 3.0f,
          "Rejected Environment must preserve World settings and content revision");
    environment_settings.intensity = 1.0f;
    check(AssetId::parse("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", environment_settings.environment.asset_id),
          "Environment fixture ID must parse");
    environment_settings.environment.expected_type = "toy3d.EnvironmentAssetData";
    check(!world.set_environment(environment_settings, nullptr) && world.content_revision() == environment_revision,
          "Configured Environment must not silently become Off when its CPU payload is missing");
    check(world.set_environment({}, nullptr), "World must explicitly clear Environment");
    Actor& parent_actor = world.spawn_actor();
    Actor& child_actor = world.spawn_actor();
    check(world.actor_count() == 2 && world.contains(parent_actor) && parent_actor.is_registered(),
          "World::spawn_actor must own and register every spawned Actor");
    check(parent_actor.actor_id() != 0u && child_actor.actor_id() > parent_actor.actor_id() &&
              world.find_actor_by_id(parent_actor.actor_id()) == &parent_actor && world.find_actor_by_id(0u) == nullptr,
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
    check(resolve_hit_proxy({2u}, hit_table, hit_target) && hit_target.actor_id == parent_actor.actor_id() &&
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
    check(world.find_actor_by_id(destroyed_actor_id) == nullptr && replacement_actor.actor_id() > destroyed_actor_id &&
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
          "World end_play must end Actors and Components without automatically enabling Component Tick");

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
    check(nearly_equal(camera.near_clip(), 10.0f) && nearly_equal(camera.far_clip(), 100000.0f),
          "Default camera clipping distances must preserve 0.1 m / 1000 m in centimeters");
    check(camera.set_perspective(75.0f, 0.25f, 500.0f) && !camera.set_perspective(180.0f, 0.25f, 500.0f) &&
              nearly_equal(camera.vertical_fov_degrees(), 75.0f),
          "CameraComponent must retain valid camera properties without building ViewportFrame");

    PointLightComponent& point = world.spawn_actor().create_component<PointLightComponent>();
    check(nearly_equal(point.range(), 1000.0f), "Default point range must preserve 10 meters in centimeters");
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
