#pragma once

#include "reflection/reflection_macros.h"

#include <cstdint>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.Texture2DAssetData", 1)
    struct Texture2DAssetData
    {
        TOY3D_PROPERTY("width", Visible)
        std::uint32_t width = 0;

        TOY3D_PROPERTY("height", Visible)
        std::uint32_t height = 0;

        TOY3D_PROPERTY("pixel_format", Visible)
        std::uint32_t pixel_format = 0;

        TOY3D_PROPERTY("mip_count", Visible)
        std::uint32_t mip_count = 0;
    };
} // namespace toy3d
