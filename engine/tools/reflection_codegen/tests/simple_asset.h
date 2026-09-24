#pragma once

#include "reflection/reflection_macros.h"

#include <cstdint>
#include <string>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.SimpleAsset", 1)
    struct SimpleAsset
    {
        TOY3D_PROPERTY("title", Edit, Category("General"))
        std::string title;

        TOY3D_PROPERTY("count", Edit, Range(0, 10), Unit("items"))
        std::int32_t count = 0;

        TOY3D_PROPERTY("dependency", Edit, AssetType("toy3d.ModelAsset"))
        std::string dependency;

        TOY3D_PROPERTY("derived", Visible | Transient)
        float derived = 0.0f;

        TOY3D_PROPERTY("hidden")
        std::int32_t hidden = 0;

        std::int32_t runtime_cache = 0;
    };
} // namespace toy3d
