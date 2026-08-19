#pragma once

#include "math/math.h"

namespace toy3d
{
    struct AxisAlignedBounds
    {
        vec3 minimum{0.0f};
        vec3 maximum{0.0f};
    };
}
