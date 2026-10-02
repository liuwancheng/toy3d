#pragma once

#include "gamescene/world/world_types.h"

#include <cstdint>

namespace toy3d
{
    class Actor;
    class World;

    class ActorComponent
    {
      public:
        explicit ActorComponent(Actor& owner);
        virtual ~ActorComponent() = default;

        ActorComponent(const ActorComponent&) = delete;
        ActorComponent& operator=(const ActorComponent&) = delete;

        Actor& owner() const
        {
            return owner_;
        }
        std::uint32_t component_id() const
        {
            return component_id_;
        }
        World& world() const;
        bool is_registered() const
        {
            return registered_;
        }
        bool is_initialized() const
        {
            return initialized_;
        }
        bool has_begun_play() const
        {
            return begun_play_;
        }
        bool is_tick_enabled() const
        {
            return tick_enabled_;
        }
        void set_tick_enabled(bool enabled);

      protected:
        // World dispatches registered, playing components after the Actor phase.
        // Report domain errors in the override; false contributes to World::tick failure.
        virtual bool tick_component(const WorldTickContext&)
        {
            return true;
        }
        virtual void on_register()
        {
        }
        virtual void on_initialize()
        {
        }
        virtual void on_begin_play()
        {
        }
        virtual void on_end_play(EndPlayReason)
        {
        }
        virtual void on_unregister()
        {
        }

      private:
        friend class Actor;
        friend class World;

        void register_component();
        void initialize_component();
        void begin_play();
        void end_play(EndPlayReason reason);
        void unregister_component();
        bool tick_registered_component(const WorldTickContext& context);

        Actor& owner_;
        std::uint32_t component_id_ = 0;
        bool registered_ = false;
        bool initialized_ = false;
        bool begun_play_ = false;
        bool tick_enabled_ = false;
    };
} // namespace toy3d
