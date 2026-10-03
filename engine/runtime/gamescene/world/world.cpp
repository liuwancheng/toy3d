#include "gamescene/world/world.h"

#include <algorithm>
#include <limits>

#include "logging/logger.h"
#include "math/scalar_math.h"
#include "rendercore/scene_interface.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // World::LifecycleDispatchScope: protects nested lifecycle callback dispatch.
    // --------------------------------------------------------------------------
    World::LifecycleDispatchScope::LifecycleDispatchScope(World& world)
        : world_(world), previous_dispatching_(world.dispatching_lifecycle_)
    {
        world_.dispatching_lifecycle_ = true;
    }

    World::LifecycleDispatchScope::~LifecycleDispatchScope()
    {
        world_.dispatching_lifecycle_ = previous_dispatching_;
    }

    // --------------------------------------------------------------------------
    // World: owns Actors and dispatches lifecycle and gameplay phases on GT.
    // --------------------------------------------------------------------------
    World::~World()
    {
        end_play();
        if (scene_interface_ != nullptr)
        {
            unbind_scene();
        }
        // Destroy components while every World service observed by their hooks is still alive.
        LifecycleDispatchScope dispatch(*this);
        while (!actors_.empty())
        {
            actors_.back()->mark_pending_destroy();
            destroy_actor_immediate(actors_.end() - 1);
        }
    }

    void World::initialize()
    {
        if (lifecycle_state_ != WorldLifecycleState::Created)
        {
            return;
        }

        lifecycle_state_ = WorldLifecycleState::Initialized;
        {
            LifecycleDispatchScope dispatch(*this);
            for (std::size_t index = 0; index < actors_.size(); ++index)
            {
                actors_[index]->initialize_actor();
            }
        }
        flush_pending_destruction();
    }

    void World::begin_play()
    {
        if (lifecycle_state_ == WorldLifecycleState::Playing)
        {
            return;
        }
        initialize();

        lifecycle_state_ = WorldLifecycleState::Playing;
        {
            LifecycleDispatchScope dispatch(*this);
            for (std::size_t index = 0; index < actors_.size(); ++index)
            {
                actors_[index]->begin_play();
            }
        }
        flush_pending_destruction();
    }

    bool World::tick(double delta_seconds)
    {
        if (ticking_ || dispatching_lifecycle_)
        {
            TOY_LOG_ERROR("World tick cannot run recursively or during lifecycle callbacks.");
            return false;
        }
        if (lifecycle_state_ != WorldLifecycleState::Playing)
        {
            TOY_LOG_ERROR("A World can tick only after begin_play().");
            return false;
        }
        if (!is_finite(delta_seconds) || delta_seconds < 0.0)
        {
            TOY_LOG_ERROR("World delta time must be finite and non-negative.");
            return false;
        }
        if (frame_number_ == (std::numeric_limits<std::uint64_t>::max)() ||
            !is_finite(world_time_seconds_ + delta_seconds))
        {
            TOY_LOG_ERROR("World time cannot advance beyond its representable range.");
            return false;
        }

        world_time_seconds_ += delta_seconds;
        ++frame_number_;
        const WorldTickContext context{delta_seconds, world_time_seconds_, frame_number_};

        // Snapshot before gameplay callbacks. New registrations wait until the next frame;
        // pending Actor destruction keeps all observed component owners alive until dispatch ends.
        const auto component_ticks = component_ticks_;
        ticking_ = true;
        const std::size_t actor_count_at_tick_start = actors_.size();
        for (std::size_t index = 0; index < actor_count_at_tick_start; ++index)
        {
            Actor* actor = actors_[index].get();
            if (!actor->is_pending_destroy())
            {
                actor->tick_actor(context);
            }
        }
        bool component_success = true;
        for (auto* component : component_ticks)
        {
            if (!component->tick_registered_component(context))
            {
                TOY_LOG_ERROR("Component tick failed: Actor {}, Component {}.", component->owner().actor_id(),
                              component->component_id());
                component_success = false;
            }
        }
        ticking_ = false;
        flush_pending_destruction();
        return component_success;
    }

    void World::register_component_tick(ActorComponent& component)
    {
        if (std::find(component_ticks_.begin(), component_ticks_.end(), &component) == component_ticks_.end())
        {
            component_ticks_.push_back(&component);
        }
    }

    void World::unregister_component_tick(ActorComponent& component)
    {
        component_ticks_.erase(std::remove(component_ticks_.begin(), component_ticks_.end(), &component),
                               component_ticks_.end());
    }

    void World::end_play()
    {
        if (lifecycle_state_ != WorldLifecycleState::Playing)
        {
            return;
        }

        // Leave Playing before callbacks so Actors spawned by shutdown logic
        // cannot begin play in a World that is stopping.
        lifecycle_state_ = WorldLifecycleState::Initialized;
        {
            LifecycleDispatchScope dispatch(*this);
            const std::size_t actor_count_at_end_play_start = actors_.size();
            for (std::size_t index = actor_count_at_end_play_start; index > 0; --index)
            {
                actors_[index - 1]->end_play(EndPlayReason::WorldEndPlay);
            }
        }
        flush_pending_destruction();
    }

    World::ActorStorage::iterator World::find_actor(Actor& actor)
    {
        return std::find_if(actors_.begin(), actors_.end(),
                            [&actor](const std::unique_ptr<Actor>& candidate)
                            {
                                return candidate.get() == &actor;
                            });
    }

    std::uint32_t World::allocate_actor_id()
    {
        if (next_actor_id_ > (std::numeric_limits<std::uint32_t>::max)())
        {
            TOY_LOG_ERROR("World exhausted its 32-bit Actor IDs; new Actors cannot be picked.");
            return 0u;
        }
        return static_cast<std::uint32_t>(next_actor_id_++);
    }

    std::uint32_t World::allocate_component_id()
    {
        if (next_component_id_ > (std::numeric_limits<std::uint32_t>::max)())
        {
            TOY_LOG_ERROR("World exhausted its 32-bit Component IDs; new Components cannot be picked.");
            return 0u;
        }
        return static_cast<std::uint32_t>(next_component_id_++);
    }

    bool World::destroy_actor(Actor& actor)
    {
        const auto found = find_actor(actor);
        if (found == actors_.end())
        {
            return false;
        }

        if ((*found)->is_pending_destroy())
        {
            return true;
        }

        (*found)->mark_pending_destroy();
        mark_scene_changed();
        mark_content_changed();
        if (ticking_ || dispatching_lifecycle_)
        {
            return true;
        }

        destroy_actor_immediate(found);
        flush_pending_destruction();
        return true;
    }

    void World::destroy_actor_immediate(ActorStorage::iterator actor)
    {
        LifecycleDispatchScope dispatch(*this);
        Actor* const identity = actor->get();
        identity->end_play(EndPlayReason::Destroyed);
        identity->unregister_all_components();
        // Hooks can append Actors and reallocate actors_; resolve the iterator again.
        actors_.erase(find_actor(*identity));
    }

    void World::flush_pending_destruction()
    {
        if (ticking_ || dispatching_lifecycle_)
        {
            return;
        }
        // A callback may mark an earlier Actor pending or append a new one.
        // Resolve each removal from the current container after the previous hooks finish.
        for (;;)
        {
            const auto actor = std::find_if(actors_.begin(), actors_.end(),
                                            [](const std::unique_ptr<Actor>& candidate)
                                            {
                                                return candidate->is_pending_destroy();
                                            });
            if (actor == actors_.end())
            {
                break;
            }
            destroy_actor_immediate(actor);
        }
    }

    bool World::contains(const Actor& actor) const
    {
        return std::any_of(actors_.begin(), actors_.end(),
                           [&actor](const std::unique_ptr<Actor>& candidate)
                           {
                               return candidate.get() == &actor;
                           });
    }

    Actor* World::find_actor_by_id(std::uint32_t actor_id) const
    {
        if (actor_id == 0u)
        {
            return nullptr;
        }
        for (const std::unique_ptr<Actor>& actor : actors_)
        {
            if (actor->actor_id() == actor_id && !actor->is_pending_destroy())
            {
                return actor.get();
            }
        }
        return nullptr;
    }

    std::vector<std::uint32_t> World::actor_ids() const
    {
        std::vector<std::uint32_t> ids;
        ids.reserve(actors_.size());
        for (const std::unique_ptr<Actor>& actor : actors_)
        {
            if (!actor->is_pending_destroy() && actor->actor_id() != 0u)
            {
                ids.push_back(actor->actor_id());
            }
        }
        return ids;
    }

    void World::mark_content_changed()
    {
        content_revision_ =
            content_revision_ == (std::numeric_limits<std::uint64_t>::max)() ? 1u : content_revision_ + 1u;
    }

    void World::mark_scene_changed()
    {
        scene_generation_ =
            scene_generation_ == (std::numeric_limits<std::uint64_t>::max)() ? 1u : scene_generation_ + 1u;
    }

    bool World::set_environment(SceneEnvironmentSettings settings, TextureRef cube)
    {
        std::string error;
        Quaternion rotation;
        const SceneEnvironmentSnapshot snapshot{cube, settings.rotation, settings.intensity};
        if (!validate_scene_environment_settings(settings) ||
            settings.environment.asset_id.valid() != static_cast<bool>(cube) ||
            !try_normalize(settings.rotation, rotation) || !validate_scene_environment_snapshot(snapshot, error))
        {
            TOY_LOG_ERROR("World Environment snapshot is invalid: {}", error);
            return false;
        }
        settings.rotation = rotation;
        environment_settings_ = std::move(settings);
        environment_cube_ = std::move(cube);
        mark_content_changed();
        if (scene_interface_)
        {
            scene_interface_->update_environment(
                {environment_cube_, environment_settings_.rotation, environment_settings_.intensity});
        }
        return true;
    }

    bool World::bind_scene(SceneInterface& scene)
    {
        if (scene_interface_ != nullptr)
        {
            return false;
        }

        // The pointer is made visible only to registered PrimitiveComponent lifecycle
        // while ownership-transfer Add commands are being issued. Contract violations
        // fail fast and therefore do not create a recoverable partial-bind branch.
        scene_interface_ = &scene;
        scene_interface_->update_environment(
            {environment_cube_, environment_settings_.rotation, environment_settings_.intensity});
        for (const std::unique_ptr<Actor>& actor : actors_)
        {
            actor->create_render_state_for_registered_components();
        }
        return true;
    }

    bool World::unbind_scene()
    {
        if (scene_interface_ == nullptr)
        {
            return false;
        }

        for (std::size_t index = actors_.size(); index > 0; --index)
        {
            actors_[index - 1]->destroy_render_state_for_registered_components();
        }
        scene_interface_->update_environment({});
        scene_interface_ = nullptr;
        return true;
    }
} // namespace toy3d
