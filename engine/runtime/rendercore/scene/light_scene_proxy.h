#pragma once

#include "math/vector3.h"

namespace toy3d
{
    enum class LightKind { Directional, Point };

    struct LightSceneData
    {
        LightKind kind = LightKind::Directional;
        Vector3 position;
        Vector3 direction{0, 0, 1};
        Vector3 color{1.0f};
        float intensity = 1.0f;
        float range = 10.0f;
        int priority = 0;
        bool enabled = true;
        bool cast_shadows = false;
        float shadow_distance = 100.0f;
        float shadow_distance_fade_fraction = 0.1f;
        float shadow_bias = 0.5f;
        float shadow_slope_bias = 0.5f;
    };

    // Scene owns the RT mirror; Components retain only an opaque identity.
    struct LightSceneProxy
    {
        LightSceneData data;
    };
}
