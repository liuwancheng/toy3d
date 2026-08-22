#include "gamescene/world/world.h"

#include <algorithm>

namespace toy3d
{
    bool World::destroy_actor(Actor& actor)
    {
        const auto found = std::find_if(
            actors_.begin(),
            actors_.end(),
            [&actor](const std::unique_ptr<Actor>& candidate)
            {
                return candidate.get() == &actor;
            });
        if (found == actors_.end())
        {
            return false;
        }

        (*found)->unregister_all_components();
        actors_.erase(found);
        return true;
    }

    bool World::contains(const Actor& actor) const
    {
        return std::any_of(
            actors_.begin(),
            actors_.end(),
            [&actor](const std::unique_ptr<Actor>& candidate)
            {
                return candidate.get() == &actor;
            });
    }
}
