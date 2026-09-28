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
    };

    // Scene owns the RT mirror; Components retain only an opaque identity.
    struct LightSceneProxy
    {
        LightSceneData data;
    };
}
