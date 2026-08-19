#pragma once

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
        ~Actor() = default;

        Actor(const Actor&) = delete;
        Actor& operator=(const Actor&) = delete;

        World& world() const { return world_; }
        SceneComponent* root_component() const { return root_component_; }

        template<typename Component = SceneComponent, typename... Args>
        Component& create_scene_component(Args&&... args)
        {
            static_assert(std::is_base_of<SceneComponent, Component>::value,
                "Component must derive from SceneComponent");
            auto component = std::make_unique<Component>(
                *this, std::forward<Args>(args)...);
            Component& result = *component;
            components_.push_back(std::move(component));
            if (root_component_ == nullptr)
            {
                root_component_ = &result;
            }
            return result;
        }

        bool set_root_component(SceneComponent* component);

    private:
        friend class World;

        bool owns_component(const SceneComponent& component) const;

        World& world_;
        std::vector<std::unique_ptr<SceneComponent>> components_;
        SceneComponent* root_component_ = nullptr;
    };
}
