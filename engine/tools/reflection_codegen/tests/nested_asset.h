#pragma once

#include "math/vector3.h"
#include "reflection/reflection_macros.h"

#include <variant>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_ENUM("toy3d.ShapeMode")
    enum class ShapeMode
    {
        Solid = 1,
        Trigger = 2
    };

    TOY3D_REFLECT_TYPE("toy3d.BoxData", 1)
    struct BoxData
    {
        TOY3D_PROPERTY("size", Edit)
        Vector3 size;
    };

    TOY3D_REFLECT_TYPE("toy3d.CapsuleData", 1)
    struct CapsuleData
    {
        TOY3D_PROPERTY("radius", Edit)
        float radius = 0.0f;
    };

    TOY3D_REFLECT_TYPE("toy3d.NestedAsset", 1)
    struct NestedAsset
    {
        TOY3D_PROPERTY("boxes", Edit)
        std::vector<BoxData> boxes;

        // variant makes the finite shape branches explicit for generated data.
        TOY3D_PROPERTY("shape", Edit)
        std::variant<BoxData, CapsuleData> shape;

        TOY3D_PROPERTY("mode", Edit)
        ShapeMode mode = ShapeMode::Solid;
    };
} // namespace toy3d
