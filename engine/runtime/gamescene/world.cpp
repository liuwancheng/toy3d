#include "gamescene/world.h"

namespace toy3d
{
    Actor& World::create_actor()
    {
        auto actor = std::make_unique<Actor>(*this);
        Actor& result = *actor;
        actors_.push_back(std::move(actor));
        return result;
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
}
