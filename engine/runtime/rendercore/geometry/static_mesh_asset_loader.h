#pragma once

#include "rendercore/geometry/static_mesh.h"
#include "asset/mesh/static_mesh_asset.h"
#include "rendercore/geometry/mesh_material_loader.h"

namespace toy3d
{
    StaticMeshRef create_static_mesh_from_asset(const StaticMeshAssetGeometry& geometry,
                                                const MaterialInterfaceRef& default_material,
                                                const MeshMaterialResolver& resolver = {});
} // namespace toy3d
