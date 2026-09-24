#pragma once

#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.InvalidVisibleUsage", 1)
    struct InvalidVisibleUsage
    {
        TOY3D_PROPERTY("value", Edit | Visible)
        float value = 0.0f;
    };
} // namespace toy3d
