#include "gamescene/actor/actor_type_registry.h"

#include <utility>
#include <algorithm>

#include "gamescene/actor/camera_actor.h"
#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/world/world.h"

namespace toy3d
{
    namespace
    {
        bool validate_empty(const ReflectedValue& value)
        {
            return value.type == "toy3d.ActorSettings" && value.schema_version == 1u &&
                   value.bytes == std::vector<std::uint8_t>{0, 0, 0, 0};
        }
        bool capture_empty(const Actor&, ReflectedValue& value)
        {
            value = {"toy3d.ActorSettings", 1, {0, 0, 0, 0}};
            return true;
        }
        bool apply_empty(Actor&, const ReflectedValue& value)
        {
            return validate_empty(value);
        }
        template <typename T> Actor& create_actor(World& world)
        {
            return world.spawn_actor<T>();
        }
    } // namespace

    ActorTypeRegistry::ActorTypeRegistry()
    {
        const bool added = add({"toy3d.Actor",
                                "Actor",
                                typeid(Actor),
                                "toy3d.ActorSettings",
                                create_actor<Actor>,
                                validate_empty,
                                capture_empty,
                                apply_empty,
                                {}}) &&
                           add({"toy3d.StaticMeshActor",
                                "Static Mesh",
                                typeid(StaticMeshActor),
                                "toy3d.ActorSettings",
                                create_actor<StaticMeshActor>,
                                validate_empty,
                                capture_empty,
                                apply_empty,
                                {}}) &&
                           add({"toy3d.DirectionalLightActor",
                                "Directional Light",
                                typeid(DirectionalLightActor),
                                "toy3d.ActorSettings",
                                create_actor<DirectionalLightActor>,
                                validate_empty,
                                capture_empty,
                                apply_empty,
                                {}}) &&
                           add({"toy3d.PointLightActor",
                                "Point Light",
                                typeid(PointLightActor),
                                "toy3d.ActorSettings",
                                create_actor<PointLightActor>,
                                validate_empty,
                                capture_empty,
                                apply_empty,
                                {}}) &&
                           add({"toy3d.CameraActor",
                                "Camera",
                                typeid(CameraActor),
                                "toy3d.ActorSettings",
                                create_actor<CameraActor>,
                                validate_empty,
                                capture_empty,
                                apply_empty,
                                {}});
        failed_ = !added;
    }

    bool ActorTypeRegistry::add(ActorType type)
    {
        if (frozen_ || failed_ ||
            (type.name.empty() || type.name.size() > 254u ||
             type.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") !=
                 std::string::npos) ||
            type.runtime_type == typeid(void) || type.display_name.empty() || type.property_type.empty() ||
            !type.create || !type.validate || !type.capture || !type.apply ||
            (!type.default_mesh.empty() && type.default_mesh != "Cube" && type.default_mesh != "Plane"))
        {
            failed_ = true;
            return false;
        }
        for (const auto& existing : types_)
        {
            if (existing.name == type.name || existing.runtime_type == type.runtime_type)
            {
                failed_ = true;
                return false;
            }
        }
        types_.push_back(std::move(type));
        return true;
    }

    bool ActorTypeRegistry::freeze(const TypeRegistry& types)
    {
        if (failed_ || !types.frozen())
        {
            return false;
        }
        for (const auto& type : types_)
        {
            const auto* properties = types.find(type.property_type);
            if (!properties || !properties->enum_values.empty())
            {
                return false;
            }
        }
        frozen_ = true;
        return true;
    }

    const ActorType* ActorTypeRegistry::find(const std::string& name) const
    {
        for (const auto& type : types_)
        {
            if (type.name == name)
            {
                return &type;
            }
        }
        return nullptr;
    }
    const ActorType* ActorTypeRegistry::find(const Actor& actor) const
    {
        for (const auto& type : types_)
        {
            if (type.runtime_type == typeid(actor))
            {
                return &type;
            }
        }
        return nullptr;
    }
    Actor* ActorTypeRegistry::create(World& world, const std::string& name) const
    {
        const auto* type = find(name);
        if (!type)
        {
            return nullptr;
        }
        const auto before = world.actor_ids();
        Actor& actor = type->create(world);
        std::vector<std::uint32_t> created;
        for (const auto id : world.actor_ids())
        {
            if (std::find(before.begin(), before.end(), id) == before.end())
            {
                created.push_back(id);
            }
        }
        // A factory owns exactly one new Actor in this World. Never adopt or remove
        // an old Actor when a faulty module returns an existing object.
        if (created.size() == 1u && world.contains(actor) && created.front() == actor.actor_id() &&
            std::type_index(typeid(actor)) == type->runtime_type)
        {
            return &actor;
        }
        for (const auto id : created)
        {
            if (auto* candidate = world.find_actor_by_id(id))
            {
                world.destroy_actor(*candidate);
            }
        }
        return nullptr;
    }
    bool ActorTypeRegistry::validate(const std::string& type, const ReflectedValue& properties) const
    {
        const auto* found = find(type);
        return found && properties.type == found->property_type && found->validate(properties);
    }
    bool ActorTypeRegistry::capture(const Actor& actor, ReflectedValue& properties) const
    {
        const auto* found = find(actor);
        return found && found->capture(actor, properties) && validate(found->name, properties);
    }
    bool ActorTypeRegistry::apply(Actor& actor, const ReflectedValue& properties) const
    {
        const auto* found = find(actor);
        return found && validate(found->name, properties) && found->apply(actor, properties);
    }
} // namespace toy3d
