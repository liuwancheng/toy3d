#include "gamescene/actor/actor_type_registry.h"

#include "rotating_actor.h"
#include "rotation_reflection.h"
#include "gamescene/world/world.h"

namespace toy3d
{
    namespace
    {
        bool decode_settings(const ReflectedValue& value, RotationSettings& settings)
        {
            if (value.type != "shadow_demo.RotationSettings" || value.schema_version != 1u) return false;
            ValueReader reader(value.bytes);
            return decode_value(reader, settings).succeeded() && reader.at_end() && RotatingActor::valid_settings(settings);
        }
        bool validate_rotation(const ReflectedValue& value) { RotationSettings settings; return decode_settings(value, settings); }
        bool capture_rotation(const Actor& actor, ReflectedValue& value)
        {
            const auto* rotating = dynamic_cast<const RotatingActor*>(&actor);
            if (!rotating) return false;
            ValueWriter writer;
            if (!encode_value(writer, rotating->rotation_settings()).succeeded()) return false;
            value.type = "shadow_demo.RotationSettings"; value.schema_version = 1; value.bytes = writer.bytes();
            return true;
        }
        bool apply_rotation(Actor& actor, const ReflectedValue& value)
        {
            RotationSettings settings;
            auto* rotating = dynamic_cast<RotatingActor*>(&actor);
            return rotating && decode_settings(value, settings) && rotating->set_rotation_settings(settings);
        }
        Actor& create_rotation(World& world) { return world.spawn_actor<RotatingActor>(); }
        bool register_game(TypeRegistry& types, ActorTypeRegistry& actors)
        {
            return register_rotation_types(types).succeeded() && actors.add({"shadow_demo.RotatingActor", "Rotating Actor", typeid(RotatingActor),
                "shadow_demo.RotationSettings", create_rotation, validate_rotation, capture_rotation, apply_rotation, "Cube", true});
        }
    }

    GameModuleRegistration linked_game_module() { return {"ShadowDemo", register_game}; }
}
