#pragma once

#include "reflection/reflection_macros.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.StaticMeshAssetData", 3)
    struct StaticMeshAssetData
    {
        TOY3D_PROPERTY("valid_tangent_frame", Visible)
        bool valid_tangent_frame = false;

        TOY3D_PROPERTY("material_slots", Visible)
        std::vector<std::string> material_slots;

        TOY3D_PROPERTY("vertex_count", Visible)
        std::uint32_t vertex_count = 0;

        TOY3D_PROPERTY("index_count", Visible)
        std::uint32_t index_count = 0;

        TOY3D_PROPERTY("geometry_segment", Visible)
        std::string geometry_segment = "render_geometry";
    };
} // namespace toy3d
