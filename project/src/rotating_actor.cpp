#include "rotating_actor.h"

#include <cmath>

#include "gamescene/world/world.h"
#include "logging/logger.h"

namespace toy3d
{
    RotatingActor::RotatingActor(World& world) : Actor(world) { set_tick_enabled(true); }

    bool RotatingActor::valid_settings(const RotationSettings& settings)
    {
        Vector3 axis;
        return std::isfinite(settings.speed_degrees_per_second) &&
            std::abs(settings.speed_degrees_per_second) <= 36000.0f && try_normalize(settings.axis, axis);
    }

    bool RotatingActor::set_rotation_settings(const RotationSettings& settings)
    {
        if (!valid_settings(settings)) return false;
        if (settings_.enabled == settings.enabled && settings_.axis == settings.axis &&
            settings_.speed_degrees_per_second == settings.speed_degrees_per_second) return true;
        settings_ = settings;
        set_tick_enabled(true);
        world().mark_content_changed();
        return true;
    }

    void RotatingActor::tick(const WorldTickContext& context)
    {
        if (!settings_.enabled || settings_.speed_degrees_per_second == 0.0f || context.delta_seconds == 0.0) return;
        SceneComponent* root = root_component();
        if (!root)
        { TOY_LOG_ERROR("RotatingActor {} has no root component; rotation stopped.", actor_id()); set_tick_enabled(false); return; }
        // Reduce the angle in double precision before conversion, including long frames.
        const double degrees = std::fmod(static_cast<double>(settings_.speed_degrees_per_second) * context.delta_seconds, 360.0);
        Quaternion step, rotation;
        Transform transform = root->local_transform();
        if (!std::isfinite(degrees) || !try_make_quaternion_from_axis_angle(settings_.axis, to_radians(Degrees(static_cast<float>(degrees))), step) ||
            !try_normalize(transform.rotation * step, rotation))
        { TOY_LOG_ERROR("RotatingActor {} rejected an invalid rotation; rotation stopped.", actor_id()); set_tick_enabled(false); return; }
        transform.rotation = rotation;
        if (!root->set_local_transform(transform))
        { TOY_LOG_ERROR("RotatingActor {} could not publish its Transform; rotation stopped.", actor_id()); set_tick_enabled(false); }
    }
}
