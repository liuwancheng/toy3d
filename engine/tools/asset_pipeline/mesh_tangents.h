#pragma once

#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    // Corner tangent generation may split vertices. The remap copies ALL caller-owned
    // attributes (notably skin influences) from the original vertex into each result.
    struct MeshTangentBuildResult
    {
        StaticMeshAssetGeometry geometry;
        std::vector<std::uint32_t> source_vertices;
    };

    AssetResult<MeshTangentBuildResult> build_mesh_tangents(const StaticMeshAssetGeometry& source);
} // namespace toy3d
