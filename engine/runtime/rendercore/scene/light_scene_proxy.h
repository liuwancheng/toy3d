#pragma once

#include "math/vector3.h"

namespace toy3d
{
    enum class LightKind { Directional, Point };

    struct LightSceneData
    {
        static constexpr int k_max_shadow_cascades = 3;
        static constexpr int k_min_shadow_resolution = 512;
        static constexpr int k_max_shadow_resolution = 2048;
        static constexpr int k_default_shadow_resolution = 2048;
        LightKind kind = LightKind::Directional;
        Vector3 position;
        Vector3 direction{0, 0, 1};
        Vector3 color{1.0f};
        float intensity = 1.0f;
        float range = 1000.0f;
        int priority = 0;
        bool enabled = true;
        bool cast_shadows = false;
        int shadow_cascade_count = 1;
        float cascade_distribution_exponent = 3.0f;
        int shadow_map_resolution = 2048;
        float shadow_distance = 10000.0f;
        float shadow_distance_fade_fraction = 0.1f;
        float shadow_bias = 0.5f;
        float shadow_slope_bias = 0.5f;
        float shadow_receiver_bias = 0.9f;
    };

    // Scene owns the RT mirror; Components retain only an opaque identity.
    struct LightSceneProxy
    {
        LightSceneData data;
    };
}
