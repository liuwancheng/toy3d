#pragma once

#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.InvalidUsage", 1)
    struct InvalidUsage
    {
        TOY3D_PROPERTY("value", Edit | Transient)
        float value = 0.0f;
    };
} // namespace toy3d
