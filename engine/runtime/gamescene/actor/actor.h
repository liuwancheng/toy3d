#pragma once

#include "gamescene/component/actor_component.h"
#include "gamescene/component/scene_component.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace toy3d
{
    class World;

    class Actor
    {
    public:
        explicit Actor(World& world) : world_(world) {}
        virtual ~Actor();

        Actor(const Actor&) = delete;
        Actor& operator=(const Actor&) = delete;

        World& world() const { return world_; }
        bool is_registered() const { return registered_; }
        SceneComponent* root_component() const { return root_component_; }

        template<typename Component, typename... Args>
        Component& create_component(Args&&... args)
        {
            static_assert(std::is_base_of<ActorComponent, Component>::value,
                "Component must derive from ActorComponent");

            auto component = std::make_unique<Component>(
                *this, std::forward<Args>(args)...);
            Component& result = *component;
            components_.push_back(std::move(component));
            if (registered_)
            {
                result.register_component();
            }
            return result;
        }

        bool set_root_component(SceneComponent* component);

    private:
        friend class World;

        bool owns_component(const ActorComponent& component) const;
        void register_all_components();
        void unregister_all_components();

        World& world_;
        std::vector<std::unique_ptr<ActorComponent>> components_;
        SceneComponent* root_component_ = nullptr;
        bool registered_ = false;
    };
}
