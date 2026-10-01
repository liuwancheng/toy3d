#pragma once

#include "asset/asset_identity.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "reflection/reflection_macros.h"

#include <string>
#include <variant>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_ENUM("toy3d.MaterialSamplerPreset")
    enum class MaterialSamplerPreset
    {
        PointClamp = 0,
        PointWrap = 1,
        LinearClamp = 2,
        LinearWrap = 3,
        TrilinearClamp = 4,
        TrilinearWrap = 5,
        ShadowCompareClamp = 6
    };

    TOY3D_REFLECT_TYPE("toy3d.MaterialParameterOverride", 1)
    struct MaterialParameterOverride
    {
        TOY3D_PROPERTY("name", Visible)
        std::string name;

        // C++17 variant keeps the persisted parameter types closed and typed.
        TOY3D_PROPERTY("value", Edit)
        std::variant<float, Vector2, Vector3, Vector4, AssetRef, MaterialSamplerPreset> value = 0.0f;
    };

    TOY3D_REFLECT_TYPE("toy3d.MaterialAssetData", 1)
    struct MaterialAssetData
    {
        TOY3D_PROPERTY("shader_name", Visible)
        std::string shader_name;

        TOY3D_PROPERTY("overrides", Edit)
        std::vector<MaterialParameterOverride> overrides;

        TOY3D_PROPERTY("two_sided", Edit)
        bool two_sided = false;
    };

    TOY3D_REFLECT_TYPE("toy3d.MaterialInstanceAssetData", 1)
    struct MaterialInstanceAssetData
    {
        TOY3D_PROPERTY("parent", Edit)
        AssetRef parent;

        TOY3D_PROPERTY("overrides", Edit)
        std::vector<MaterialParameterOverride> overrides;
    };
}
