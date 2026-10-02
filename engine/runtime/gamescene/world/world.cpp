#include "gamescene/world/world.h"

#include <algorithm>
#include <limits>

#include "logging/logger.h"
#include "math/scalar_math.h"
#include "rendercore/scene_interface.h"

namespace toy3d
{
    World::~World()
    {
        end_play();
        if (scene_interface_ != nullptr)
        {
            unbind_scene();
        }
    }

    void World::initialize()
    {
        if (lifecycle_state_ != WorldLifecycleState::Created)
        {
            return;
        }

        lifecycle_state_ = WorldLifecycleState::Initialized;
        dispatching_lifecycle_ = true;
        for (std::size_t index = 0; index < actors_.size(); ++index)
        {
            actors_[index]->initialize_actor();
        }
        dispatching_lifecycle_ = false;
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
        dispatching_lifecycle_ = true;
        for (std::size_t index = 0; index < actors_.size(); ++index)
        {
            actors_[index]->begin_play();
        }
        dispatching_lifecycle_ = false;
        flush_pending_destruction();
    }

    bool World::tick(double delta_seconds)
    {
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
        ticking_ = false;
        flush_pending_destruction();
        return true;
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
        dispatching_lifecycle_ = true;
        const std::size_t actor_count_at_end_play_start = actors_.size();
        for (std::size_t index = actor_count_at_end_play_start; index > 0; --index)
        {
            actors_[index - 1]->end_play(EndPlayReason::WorldEndPlay);
        }
        dispatching_lifecycle_ = false;
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
        return true;
    }

    void World::destroy_actor_immediate(ActorStorage::iterator actor)
    {
        (*actor)->end_play(EndPlayReason::Destroyed);
        (*actor)->unregister_all_components();
        actors_.erase(actor);
    }

    void World::flush_pending_destruction()
    {
        for (auto actor = actors_.begin(); actor != actors_.end();)
        {
            if ((*actor)->is_pending_destroy())
            {
                (*actor)->end_play(EndPlayReason::Destroyed);
                (*actor)->unregister_all_components();
                actor = actors_.erase(actor);
            }
            else
            {
                ++actor;
            }
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
        scene_interface_ = nullptr;
        return true;
    }
} // namespace toy3d
