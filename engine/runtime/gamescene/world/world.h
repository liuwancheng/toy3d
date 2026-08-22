#pragma once

#include "gamescene/actor/actor.h"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace toy3d
{
    class World
    {
    public:
        World() = default;
        ~World() = default;

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        template<typename ActorType = Actor, typename... Args>
        ActorType& spawn_actor(Args&&... args)
        {
            static_assert(std::is_base_of<Actor, ActorType>::value,
                "ActorType must derive from Actor");

            auto actor = std::make_unique<ActorType>(
                *this, std::forward<Args>(args)...);
            ActorType& result = *actor;
            actors_.push_back(std::move(actor));
            result.register_all_components();
            return result;
        }

        bool destroy_actor(Actor& actor);
        bool contains(const Actor& actor) const;
        std::size_t actor_count() const { return actors_.size(); }

    private:
        std::vector<std::unique_ptr<Actor>> actors_;
    };
}
