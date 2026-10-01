#pragma once

#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    AssetResult<StaticMeshAssetGeometry> build_static_mesh(const MeshDescription& source);
} // namespace toy3d
