#pragma once

#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.InvalidPointer", 1)
    struct InvalidPointer
    {
        TOY3D_PROPERTY("raw", Edit)
        float* raw = nullptr;
    };
} // namespace toy3d
