#pragma once

#include "placement/placement_catalog.h"
#include "math/matrix4.h"
#include "math/vector2.h"

namespace toy3d
{
    // Normalized image coordinates use top-left origin. Placement policy is Editor-only.
    bool calculate_placement_transform(const Matrix4& view, const Matrix4& projection,
                                       const Vector3& camera_position, const Vector2& image_position,
                                       const PlacementItem& item, Transform& result, bool* on_ground = nullptr);
}
