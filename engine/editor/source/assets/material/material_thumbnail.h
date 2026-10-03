#pragma once

#include "assets/preview/asset_preview_scene.h"

#include <functional>

namespace toy3d
{
    StaticMeshAssetGeometry make_material_preview_geometry(const StaticMeshRef& mesh, bool upright_plane);
    // GT-only: the injected material domain publishes runtime owners and proxies.
    AssetStatus prepare_material_thumbnail(
        AssetPreviewScene& preview, const StaticMeshAssetGeometry& sphere, const AssetRef& reference,
        const std::function<AssetResult<MaterialInterfaceRef>(const AssetRef&)>& resolver);
} // namespace toy3d
