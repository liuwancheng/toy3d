#include "gamescene/component/actor_component.h"

#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"

namespace toy3d
{
    ActorComponent::ActorComponent(Actor& owner) : owner_(owner)
    {
    }

    World& ActorComponent::world() const
    {
        return owner_.world();
    }

    void ActorComponent::set_tick_enabled(bool enabled)
    {
        if (tick_enabled_ == enabled)
        {
            return;
        }
        tick_enabled_ = enabled;
        if (registered_)
        {
            if (enabled)
            {
                world().register_component_tick(*this);
            }
            else
            {
                world().unregister_component_tick(*this);
            }
        }
    }

    bool ActorComponent::tick_registered_component(const WorldTickContext& context)
    {
        if (!tick_enabled_ || !registered_ || !begun_play_ || !owner_.has_begun_play() || owner_.is_pending_destroy())
        {
            return true;
        }
        return tick_component(context);
    }

    void ActorComponent::register_component()
    {
        if (registered_)
        {
            return;
        }

        registered_ = true;
        if (tick_enabled_)
        {
            world().register_component_tick(*this);
        }
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

        world().unregister_component_tick(*this);
        end_play(EndPlayReason::Destroyed);
        on_unregister();
        registered_ = false;
        initialized_ = false;
        // Lifecycle hooks may change the opt-in flag while registration is still visible.
        // Withdraw any such re-registration before the component owner can be destroyed.
        world().unregister_component_tick(*this);
    }
} // namespace toy3d
