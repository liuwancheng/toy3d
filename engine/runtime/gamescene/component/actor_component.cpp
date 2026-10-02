#include "gamescene/component/actor_component.h"

#include "gamescene/actor/actor.h"

namespace toy3d
{
    ActorComponent::ActorComponent(Actor& owner) : owner_(owner)
    {
    }

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

    void ActorComponent::initialize_component()
    {
        if (initialized_ || !registered_)
        {
            return;
        }

        initialized_ = true;
        on_initialize();
    }

    void ActorComponent::begin_play()
    {
        if (begun_play_ || !initialized_)
        {
            return;
        }

        begun_play_ = true;
        on_begin_play();
    }

    void ActorComponent::end_play(EndPlayReason reason)
    {
        if (!begun_play_)
        {
            return;
        }

        begun_play_ = false;
        on_end_play(reason);
    }

    void ActorComponent::unregister_component()
    {
        if (!registered_)
        {
            return;
        }

        end_play(EndPlayReason::Destroyed);
        on_unregister();
        registered_ = false;
        initialized_ = false;
    }
} // namespace toy3d
