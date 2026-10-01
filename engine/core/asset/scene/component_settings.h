#pragma once

#include "math/length_units.h"
#include "math/vector3.h"
#include "reflection/reflection_macros.h"
#include <cstdint>

namespace toy3d
{
    TOY3D_REFLECT_TYPE("toy3d.PrimitiveSettings", 1)
    struct PrimitiveSettings
    {
        TOY3D_PROPERTY("visible", Edit)
        bool visible = true;
        TOY3D_PROPERTY("cast_shadows", Edit)
        bool cast_shadows = true;
        TOY3D_PROPERTY("receives_shadows", Edit)
        bool receives_shadows = true;
    };

    bool operator==(const PrimitiveSettings& a, const PrimitiveSettings& b);

    TOY3D_REFLECT_TYPE("toy3d.LightSettings", 1)
    struct LightSettings
    {
        TOY3D_PROPERTY("enabled", Edit)
        bool enabled = true;
        TOY3D_PROPERTY("color", Edit)
        Vector3 color = Vector3(1.0f);
        TOY3D_PROPERTY("intensity", Edit)
        float intensity = 1.0f;
        TOY3D_PROPERTY("priority", Edit)
        std::int32_t priority = 0;
    };

    bool operator==(const LightSettings& a, const LightSettings& b);
    bool is_valid(const LightSettings& settings);

    TOY3D_REFLECT_TYPE("toy3d.LocalLightSettings", 1)
    struct LocalLightSettings
    {
        TOY3D_PROPERTY("range", Edit)
        float range = meters_to_centimeters(10.0f);
    };

    bool operator==(const LocalLightSettings& a, const LocalLightSettings& b);
    bool is_valid(const LocalLightSettings& settings);

    TOY3D_REFLECT_TYPE("toy3d.DirectionalShadowSettings", 1)
    struct DirectionalShadowSettings
    {
        static constexpr int k_max_cascades = 3;
        static constexpr int k_min_resolution = 512;
        static constexpr int k_max_resolution = 2048;
        TOY3D_PROPERTY("cast_shadows", Edit)
        bool cast_shadows = false;
        TOY3D_PROPERTY("cascade_count", Edit)
        std::int32_t cascade_count = 1;
        TOY3D_PROPERTY("distribution_exponent", Edit)
        float distribution_exponent = 3.0f;
        TOY3D_PROPERTY("map_resolution", Edit)
        std::int32_t map_resolution = 2048;
        TOY3D_PROPERTY("distance", Edit)
        float distance = meters_to_centimeters(100.0f);
        TOY3D_PROPERTY("fade_fraction", Edit)
        float fade_fraction = 0.1f;
        TOY3D_PROPERTY("bias", Edit)
        float bias = 0.5f;
        TOY3D_PROPERTY("slope_bias", Edit)
        float slope_bias = 0.5f;
        TOY3D_PROPERTY("receiver_bias", Edit)
        float receiver_bias = 0.9f;
    };

    bool operator==(const DirectionalShadowSettings& a, const DirectionalShadowSettings& b);
    bool is_valid(const DirectionalShadowSettings& settings);

    TOY3D_REFLECT_TYPE("toy3d.CameraSettings", 1)
    struct CameraSettings
    {
        TOY3D_PROPERTY("vertical_fov", Edit)
        float vertical_fov = 60.0f;
        TOY3D_PROPERTY("near_clip", Edit)
        float near_clip = meters_to_centimeters(0.1f);
        TOY3D_PROPERTY("far_clip", Edit)
        float far_clip = meters_to_centimeters(1000.0f);
    };

    bool operator==(const CameraSettings& a, const CameraSettings& b);
    bool is_valid(const CameraSettings& settings);

} // namespace toy3d
