#pragma once

#include "rendercore/geometry/static_mesh.h"
#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    // Placeholder slots are explicit caller policy until material assets exist.
    StaticMeshRef create_static_mesh_from_asset(const StaticMeshAssetGeometry& geometry,
                                               const MaterialInterfaceRef& default_material);
} // namespace toy3d
