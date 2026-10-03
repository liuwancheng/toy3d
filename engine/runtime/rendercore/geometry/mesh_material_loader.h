#pragma once

#include "asset/mesh/mesh_materials.h"
#include "rendercore/material/material.h"

#include <functional>

namespace toy3d
{
    using MeshMaterialResolver = std::function<AssetResult<MaterialInterfaceRef>(const AssetRef&)>;
    AssetResult<std::vector<MaterialInterfaceRef>> load_mesh_materials(const std::vector<std::string>& slots,
                                                                       const std::vector<AssetRef>& references,
                                                                       const MaterialInterfaceRef& fallback,
                                                                       const MeshMaterialResolver& resolver,
                                                                       shader::VertexFactoryType factory,
                                                                       bool valid_tangents);
} // namespace toy3d
