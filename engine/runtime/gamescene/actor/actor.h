#pragma once

#include "gamescene/component/actor_component.h"
#include "gamescene/component/scene_component.h"
#include "gamescene/world/world_types.h"

#include <memory>
#include <cstddef>
#include <cstdint>
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
        std::uint32_t actor_id() const { return actor_id_; }
        bool is_registered() const { return registered_; }
        bool is_initialized() const { return initialized_; }
        bool has_begun_play() const { return begun_play_; }
        bool is_pending_destroy() const { return pending_destroy_; }
        bool is_tick_enabled() const { return tick_enabled_; }
        void set_tick_enabled(bool enabled) { tick_enabled_ = enabled; }
        SceneComponent* root_component() const { return root_component_; }
        std::size_t component_count() const { return components_.size(); }

        template <typename Component, typename... Args> Component& create_component(Args&&... args)
        {
            static_assert(std::is_base_of<ActorComponent, Component>::value,
                          "Component must derive from ActorComponent");

            auto component = std::make_unique<Component>(*this, std::forward<Args>(args)...);
            Component& result = *component;
            result.component_id_ = allocate_component_id();
            components_.push_back(std::move(component));
            if (registered_)
            {
                result.register_component();
            }
            if (initialized_)
            {
                result.initialize_component();
            }
            if (begun_play_)
            {
                result.begin_play();
            }
            mark_content_changed();
            return result;
        }

        bool set_root_component(SceneComponent* component);
        ActorComponent* find_component_by_id(std::uint32_t component_id) const;
        std::vector<std::uint32_t> component_ids() const;

      protected:
        virtual void on_initialize() {}
        virtual void on_begin_play() {}
        virtual void tick(const WorldTickContext&) {}
        virtual void on_end_play(EndPlayReason) {}

      private:
        friend class World;

        bool owns_component(const ActorComponent& component) const;
        std::uint32_t allocate_component_id();
        void mark_content_changed();
        void register_all_components();
        void initialize_actor();
        void begin_play();
        void tick_actor(const WorldTickContext& context);
        void end_play(EndPlayReason reason);
        void unregister_all_components();
        void create_render_state_for_registered_components();
        void destroy_render_state_for_registered_components();
        void mark_pending_destroy() { pending_destroy_ = true; }

        World& world_;
        std::uint32_t actor_id_ = 0;
        std::vector<std::unique_ptr<ActorComponent>> components_;
        SceneComponent* root_component_ = nullptr;
        bool registered_ = false;
        bool initialized_ = false;
        bool begun_play_ = false;
        bool pending_destroy_ = false;
        bool tick_enabled_ = false;
    };
} // namespace toy3d
