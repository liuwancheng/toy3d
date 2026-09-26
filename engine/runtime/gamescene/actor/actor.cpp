#include "gamescene/actor/actor.h"

#include <algorithm>

#include "gamescene/component/primitive_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"

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
        return std::any_of(components_.begin(), components_.end(),
                           [&component](const std::unique_ptr<ActorComponent>& candidate)
                           { return candidate.get() == &component; });
    }

    std::uint32_t Actor::allocate_component_id()
    {
        return world_.allocate_component_id();
    }

    ActorComponent* Actor::find_component_by_id(std::uint32_t component_id) const
    {
        if (component_id == 0u)
            return nullptr;
        for (const std::unique_ptr<ActorComponent>& component : components_)
        {
            if (component->component_id() == component_id)
                return component.get();
        }
        return nullptr;
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

    void Actor::create_render_state_for_registered_primitives()
    {
        for (const std::unique_ptr<ActorComponent>& component : components_)
        {
            PrimitiveComponent* const primitive = dynamic_cast<PrimitiveComponent*>(component.get());
            if (primitive != nullptr && primitive->is_registered())
            {
                primitive->create_render_state();
            }
        }
    }

    void Actor::destroy_render_state_for_registered_primitives()
    {
        for (std::size_t index = components_.size(); index > 0; --index)
        {
            PrimitiveComponent* const primitive = dynamic_cast<PrimitiveComponent*>(components_[index - 1].get());
            if (primitive != nullptr)
            {
                primitive->destroy_render_state();
            }
        }
    }
} // namespace toy3d
