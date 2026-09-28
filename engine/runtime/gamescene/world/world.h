#pragma once

#include "gamescene/actor/actor.h"
#include "gamescene/world/world_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace toy3d
{
    class SceneInterface;

    class World
    {
      public:
        World() = default;
        ~World();

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        template <typename ActorType = Actor, typename... Args> ActorType& spawn_actor(Args&&... args)
        {
            static_assert(std::is_base_of<Actor, ActorType>::value, "ActorType must derive from Actor");

            auto actor = std::make_unique<ActorType>(*this, std::forward<Args>(args)...);
            ActorType& result = *actor;
            // World-local Actor IDs are never reused, including after destruction.
            result.actor_id_ = allocate_actor_id();
            actors_.push_back(std::move(actor));
            mark_scene_changed();
            result.register_all_components();
            if (lifecycle_state_ != WorldLifecycleState::Created)
            {
                result.initialize_actor();
            }
            if (lifecycle_state_ == WorldLifecycleState::Playing)
            {
                result.begin_play();
            }
            return result;
        }

        void initialize();
        void begin_play();
        bool tick(double delta_seconds);
        void end_play();
        bool destroy_actor(Actor& actor);
        bool contains(const Actor& actor) const;
        Actor* find_actor_by_id(std::uint32_t actor_id) const;
        std::vector<std::uint32_t> actor_ids() const;
        std::uint64_t scene_generation() const { return scene_generation_; }
        void mark_scene_changed();
        bool bind_scene(SceneInterface& scene);
        bool unbind_scene();
        SceneInterface* scene_interface() const { return scene_interface_; }
        std::size_t actor_count() const { return actors_.size(); }
        WorldLifecycleState lifecycle_state() const { return lifecycle_state_; }
        double world_time_seconds() const { return world_time_seconds_; }
        std::uint64_t frame_number() const { return frame_number_; }
        bool is_ticking() const { return ticking_; }

      private:
        friend class Actor;
        using ActorStorage = std::vector<std::unique_ptr<Actor>>;

        ActorStorage::iterator find_actor(Actor& actor);
        std::uint32_t allocate_actor_id();
        std::uint32_t allocate_component_id();
        void destroy_actor_immediate(ActorStorage::iterator actor);
        void flush_pending_destruction();

        ActorStorage actors_;
        std::uint64_t next_actor_id_ = 1;
        std::uint64_t next_component_id_ = 1;
        std::uint64_t scene_generation_ = 1;
        WorldLifecycleState lifecycle_state_ = WorldLifecycleState::Created;
        double world_time_seconds_ = 0.0;
        std::uint64_t frame_number_ = 0;
        bool ticking_ = false;
        bool dispatching_lifecycle_ = false;
        // World observes the stable RenderCore façade; Renderer retains all scene ownership.
        SceneInterface* scene_interface_ = nullptr;
    };
} // namespace toy3d
