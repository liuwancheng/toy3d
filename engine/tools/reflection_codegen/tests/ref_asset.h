#pragma once

#include "asset/asset_identity.h"
#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.RefAsset", 1)
    struct RefAsset
    {
        TOY3D_PROPERTY("target", Edit, AssetType("toy3d.ModelAsset"))
        AssetRef target;
    };
} // namespace toy3d
