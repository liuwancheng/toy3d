#include "gamescene/component/actor_component.h"

#include "gamescene/actor/actor.h"

namespace toy3d
{
    ActorComponent::ActorComponent(Actor& owner) : owner_(owner) {}

    World& ActorComponent::world() const
    {
        return owner_.world();
    }

    void ActorComponent::register_component()
    {
        if (registered_)
        {
            return;
        }

        registered_ = true;
        on_register();
    }

    void ActorComponent::unregister_component()
    {
        if (!registered_)
        {
            return;
        }

        on_unregister();
        registered_ = false;
    }
}
