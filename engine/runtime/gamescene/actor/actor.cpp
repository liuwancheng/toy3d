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
        for (const std::unique_ptr<ActorComponent>& component : components_)
        {
            component->register_component();
        }
    }

    void Actor::unregister_all_components()
    {
        if (!registered_)
        {
            return;
        }

        for (auto component = components_.rbegin();
            component != components_.rend();
            ++component)
        {
            (*component)->unregister_component();
        }
        registered_ = false;
    }
}
