#pragma once

#include "reflection/reflection_macros.h"

#include <cstdint>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.EnvironmentAssetData", 1)
    struct EnvironmentAssetData
    {
        TOY3D_PROPERTY("face_size", Visible)
        std::uint32_t face_size = 0;

        TOY3D_PROPERTY("mip_count", Visible)
        std::uint32_t mip_count = 0;

        TOY3D_PROPERTY("algorithm_version", Visible)
        std::uint32_t algorithm_version = 1;

        TOY3D_PROPERTY("orientation_version", Visible)
        std::uint32_t orientation_version = 1;
    };
} // namespace toy3d
