#pragma once

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

    protected:
        virtual void on_register() {}
        virtual void on_unregister() {}

    private:
        friend class Actor;

        void register_component();
        void unregister_component();

        Actor& owner_;
        bool registered_ = false;
    };
}
