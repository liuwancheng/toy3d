#pragma once

#include "math/vector3.h"
#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.CollisionBox", 2)
    struct CollisionBox
    {
        TOY3D_PROPERTY("half_extents", Edit)
        Vector3 half_extents;
    };
} // namespace toy3d
