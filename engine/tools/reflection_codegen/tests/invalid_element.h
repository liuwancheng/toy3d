#pragma once

#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.InvalidElement", 1)
    struct InvalidElement
    {
        TOY3D_PROPERTY("items", Edit)
        std::vector<UnregisteredData> items;
    };
} // namespace toy3d
