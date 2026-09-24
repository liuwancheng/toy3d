#pragma once

#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.InvalidPrivate", 1)
    struct InvalidPrivate
    {
      private:
        TOY3D_PROPERTY("secret", Edit)
        float secret = 0.0f;
    };
} // namespace toy3d
