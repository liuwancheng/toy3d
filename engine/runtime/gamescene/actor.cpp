#include "gamescene/actor.h"

#include "logging/logger.h"

#include <algorithm>

namespace toy3d
{
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

    bool Actor::owns_component(const SceneComponent& component) const
    {
        return std::any_of(
            components_.begin(),
            components_.end(),
            [&component](const std::unique_ptr<SceneComponent>& candidate)
            {
                return candidate.get() == &component;
            });
    }
}
