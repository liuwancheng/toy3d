#include "gamescene/actor/actor.h"

#include "logging/logger.h"

#include <algorithm>

namespace toy3d
{
    Actor::~Actor()
    {
        unregister_all_components();
    }

    bool Actor::set_root_component(SceneComponent* component)
    {
        if (component != nullptr && !owns_component(*component))
        {
            TOY_LOG_ERROR("An Actor root component must be owned by that Actor.");
            return false;
        }
        root_component_ = component;
        return true;
    }

    bool Actor::owns_component(const ActorComponent& component) const
    {
        return std::any_of(
            components_.begin(),
            components_.end(),
            [&component](const std::unique_ptr<ActorComponent>& candidate)
            {
                return candidate.get() == &component;
            });
    }

    void Actor::register_all_components()
    {
        if (registered_)
        {
            return;
        }

        registered_ = true;
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            components_[index]->register_component();
        }
    }

    void Actor::initialize_actor()
    {
        if (initialized_ || pending_destroy_)
        {
            return;
        }

        // Mark the Actor initialized before callbacks so components created by
        // initialization callbacks enter the same lifecycle state immediately.
        initialized_ = true;
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            components_[index]->initialize_component();
        }
        on_initialize();
    }

    void Actor::begin_play()
    {
        if (begun_play_ || !initialized_ || pending_destroy_)
        {
            return;
        }

        // The state changes before callbacks so newly created components can
        // be registered, initialized and begun in one deterministic path.
        begun_play_ = true;
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            components_[index]->begin_play();
        }
        on_begin_play();
    }

    void Actor::tick_actor(const WorldTickContext& context)
    {
        if (!tick_enabled_ || !begun_play_ || pending_destroy_)
        {
            return;
        }
        tick(context);
    }

    void Actor::end_play(EndPlayReason reason)
    {
        if (!begun_play_)
        {
            return;
        }

        begun_play_ = false;
        on_end_play(reason);
        for (std::size_t index = components_.size(); index > 0; --index)
        {
            components_[index - 1]->end_play(reason);
        }
    }

    void Actor::unregister_all_components()
    {
        if (!registered_)
        {
            return;
        }

        registered_ = false;
        for (std::size_t index = components_.size(); index > 0; --index)
        {
            components_[index - 1]->unregister_component();
        }
    }
}
