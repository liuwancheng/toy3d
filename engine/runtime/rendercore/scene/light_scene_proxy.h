#pragma once

#include "asset/scene/component_settings.h"
#include "math/vector3.h"

namespace toy3d
{
    enum class LightKind { Directional, Point };

    struct LightSceneData
    {
        static constexpr int k_max_shadow_cascades = DirectionalShadowSettings::k_max_cascades;
        static constexpr int k_min_shadow_resolution = DirectionalShadowSettings::k_min_resolution;
        static constexpr int k_max_shadow_resolution = DirectionalShadowSettings::k_max_resolution;
        static constexpr int k_default_shadow_resolution = DirectionalShadowSettings{}.map_resolution;
        LightKind kind = LightKind::Directional;
        Vector3 position;
        Vector3 direction{0, 0, 1};
        Vector3 color = LightSettings{}.color;
        float intensity = LightSettings{}.intensity;
        float range = LocalLightSettings{}.range;
        int priority = LightSettings{}.priority;
        bool enabled = LightSettings{}.enabled;
        bool cast_shadows = DirectionalShadowSettings{}.cast_shadows;
        int shadow_cascade_count = DirectionalShadowSettings{}.cascade_count;
        float cascade_distribution_exponent = DirectionalShadowSettings{}.distribution_exponent;
        int shadow_map_resolution = DirectionalShadowSettings{}.map_resolution;
        float shadow_distance = DirectionalShadowSettings{}.distance;
        float shadow_distance_fade_fraction = DirectionalShadowSettings{}.fade_fraction;
        float shadow_bias = DirectionalShadowSettings{}.bias;
        float shadow_slope_bias = DirectionalShadowSettings{}.slope_bias;
        float shadow_receiver_bias = DirectionalShadowSettings{}.receiver_bias;
    };

    // Scene owns the RT mirror; Components retain only an opaque identity.
    struct LightSceneProxy
    {
        LightSceneData data;
    };
}
