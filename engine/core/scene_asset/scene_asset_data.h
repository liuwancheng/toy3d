#pragma once

#include "asset_identity.h"
#include "math/transform.h"
#include "math/vector3.h"
#include "reflection/reflection_macros.h"

#include <string>
#include <vector>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.SceneResourceBinding", 1)
    struct SceneResourceBinding
    {
        TOY3D_PROPERTY("role")
        std::string role;
        TOY3D_PROPERTY("reference")
        AssetRef reference;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneActorData", 3)
    struct SceneActorData
    {
        TOY3D_PROPERTY("id")
        std::string id;
        TOY3D_PROPERTY("root_component_id")
        std::string root_component_id;
        TOY3D_PROPERTY("root_component_type")
        std::string root_component_type;
        TOY3D_PROPERTY("kind")
        std::string kind;
        TOY3D_PROPERTY("parent_component_id")
        std::string parent_component_id;
        TOY3D_PROPERTY("transform", Edit)
        Transform transform;
        TOY3D_PROPERTY("resources", Edit)
        std::vector<SceneResourceBinding> resources;
        TOY3D_PROPERTY("primitive_cast_shadows", Edit)
        bool primitive_cast_shadows = true;
        TOY3D_PROPERTY("primitive_receives_shadows", Edit)
        bool primitive_receives_shadows = true;
        TOY3D_PROPERTY("light_enabled", Edit)
        bool light_enabled = true;
        TOY3D_PROPERTY("light_color", Edit)
        Vector3 light_color;
        TOY3D_PROPERTY("light_intensity", Edit)
        float light_intensity = 1.0f;
        TOY3D_PROPERTY("light_range", Edit)
        float light_range = 10.0f;
        TOY3D_PROPERTY("light_priority", Edit)
        std::int32_t light_priority = 0;
        TOY3D_PROPERTY("shadow_cast_shadows", Edit)
        bool shadow_cast_shadows = false;
        TOY3D_PROPERTY("shadow_distance", Edit)
        float shadow_distance = 100.0f;
        TOY3D_PROPERTY("shadow_distance_fade_fraction", Edit)
        float shadow_distance_fade_fraction = 0.1f;
        TOY3D_PROPERTY("shadow_bias", Edit)
        float shadow_bias = 0.5f;
        TOY3D_PROPERTY("shadow_slope_bias", Edit)
        float shadow_slope_bias = 0.5f;
        TOY3D_PROPERTY("camera_vertical_fov", Edit)
        float camera_vertical_fov = 60.0f;
        TOY3D_PROPERTY("camera_near_clip", Edit)
        float camera_near_clip = 0.1f;
        TOY3D_PROPERTY("camera_far_clip", Edit)
        float camera_far_clip = 1000.0f;
    };

    TOY3D_REFLECT_TYPE("toy3d.SceneAssetData", 3)
    struct SceneAssetData
    {
        TOY3D_PROPERTY("actors", Edit)
        std::vector<SceneActorData> actors;
    };
}
