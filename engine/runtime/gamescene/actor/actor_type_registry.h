#pragma once

#include "asset/scene/scene_asset_data.h"
#include "gamescene/actor/actor.h"
#include "reflection/type_registry.h"

#include <functional>
#include <typeindex>

namespace toy3d
{
    class World;

    struct ActorType
    {
        std::string name;
        std::string display_name;
        std::type_index runtime_type{typeid(void)};
        std::string property_type = "toy3d.ActorSettings";
        Actor& (*create)(World&) = nullptr;
        bool (*validate)(const ReflectedValue&) = nullptr;
        bool (*capture)(const Actor&, ReflectedValue&) = nullptr;
        bool (*apply)(Actor&, const ReflectedValue&) = nullptr;
        // Default geometry is a placement policy; scene restore uses saved components.
        std::string default_mesh;
        bool placeable = false;
    };

    class ActorTypeRegistry
    {
      public:
        ActorTypeRegistry();
        bool add(ActorType type);
        bool freeze(const TypeRegistry& types);
        bool frozen() const { return frozen_; }
        const ActorType* find(const std::string& name) const;
        const ActorType* find(const Actor& actor) const;
        Actor* create(World& world, const std::string& name) const;
        const std::vector<ActorType>& types() const { return types_; }
        bool validate(const std::string& type, const ReflectedValue& properties) const;
        bool capture(const Actor& actor, ReflectedValue& properties) const;
        bool apply(Actor& actor, const ReflectedValue& properties) const;
      private:
        std::vector<ActorType> types_;
        bool frozen_ = false;
        bool failed_ = false;
    };

    // Static module hooks are supplied by the host; Runtime never depends on a game.
    struct GameModuleRegistration
    {
        std::string name;
        bool (*register_types)(TypeRegistry&, ActorTypeRegistry&) = nullptr;
    };
}
