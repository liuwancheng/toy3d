#pragma once

#include "math/matrix4.h"

namespace toy3d
{
    // Render-side canonical values for the Object logical Binding Group.
    // The value is copied from PrimitiveSceneProxy state and contains no Game,
    // RHI, or backend ownership.
    struct PrimitiveUniformShaderParameters
    {
        Matrix4 object_to_world;
    };
}
