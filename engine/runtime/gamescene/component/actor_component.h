#pragma once

#include "gamescene/world/world_types.h"

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

        Actor& owner() const { return owner_; }
        World& world() const;
        bool is_registered() const { return registered_; }
        bool is_initialized() const { return initialized_; }
        bool has_begun_play() const { return begun_play_; }

    protected:
        virtual void on_register() {}
        virtual void on_initialize() {}
        virtual void on_begin_play() {}
        virtual void on_end_play(EndPlayReason) {}
        virtual void on_unregister() {}

    private:
        friend class Actor;

        void register_component();
        void initialize_component();
        void begin_play();
        void end_play(EndPlayReason reason);
        void unregister_component();

        Actor& owner_;
        bool registered_ = false;
        bool initialized_ = false;
        bool begun_play_ = false;
    };
}
