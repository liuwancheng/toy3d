#pragma once

#include "math/math.h"

#include "glm/gtc/quaternion.hpp"

namespace toy3d
{
    struct SceneTransform
    {
        vec3 translation{0.0f};
        quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        vec3 scale{1.0f};
    };

    mat4x4 make_transform_matrix(const SceneTransform& transform);
}
