#pragma once

#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.CameraAsset", 1)
    struct CameraAsset
    {
        TOY3D_PROPERTY("near", Edit)
        float near = 1.0f;

        TOY3D_PROPERTY("far", Edit)
        float far = 10.0f;
    };
} // namespace toy3d
