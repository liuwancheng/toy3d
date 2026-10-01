#pragma once

#include "asset/asset_file.h"
#include "math/vector2.h"
#include "math/vector3.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    struct MeshCorner
    {
        std::uint32_t vertex = 0;
        Vector3 normal{0, 1, 0};
        Vector2 uv0;
        std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    };

    struct MeshTriangle
    {
        std::array<std::uint32_t, 3> corners{};
        std::uint32_t material_slot = 0;
    };

    // Corner attributes keep hard edges and UV seams independent of positions.
    struct MeshDescription
    {
        std::vector<Vector3> positions;
        std::vector<MeshCorner> corners;
        std::vector<MeshTriangle> triangles;
        std::vector<std::string> material_slots;
    };

    AssetStatus validate_mesh_description(const MeshDescription& mesh);
    AssetResult<std::vector<std::uint8_t>> encode_mesh_description(const MeshDescription& mesh);
    AssetResult<MeshDescription> decode_mesh_description(const std::vector<std::uint8_t>& bytes);
} // namespace toy3d
