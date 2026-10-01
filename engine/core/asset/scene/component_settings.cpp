#include "asset/scene/component_settings.h"

#include "math/geometry/convex_volume.h"
#include "math/matrix_construction.h"
#include "math/scalar_math.h"

namespace toy3d
{
    bool operator==(const PrimitiveSettings& a, const PrimitiveSettings& b)
    {
        return a.visible == b.visible &&
               a.cast_shadows == b.cast_shadows &&
               a.receives_shadows == b.receives_shadows;
    }

    bool operator==(const LightSettings& a, const LightSettings& b)
    {
        return a.enabled == b.enabled &&
               a.color == b.color &&
               a.intensity == b.intensity &&
               a.priority == b.priority;
    }

    bool operator==(const LocalLightSettings& a, const LocalLightSettings& b)
    {
        return a.range == b.range;
    }

    bool operator==(const DirectionalShadowSettings& a, const DirectionalShadowSettings& b)
    {
        return a.cast_shadows == b.cast_shadows &&
               a.cascade_count == b.cascade_count &&
               a.distribution_exponent == b.distribution_exponent &&
               a.map_resolution == b.map_resolution &&
               a.distance == b.distance &&
               a.fade_fraction == b.fade_fraction &&
               a.bias == b.bias &&
               a.slope_bias == b.slope_bias &&
               a.receiver_bias == b.receiver_bias;
    }

    bool operator==(const CameraSettings& a, const CameraSettings& b)
    {
        return a.vertical_fov == b.vertical_fov &&
               a.near_clip == b.near_clip &&
               a.far_clip == b.far_clip;
    }

    bool is_valid(const LightSettings& settings)
    {
        return is_finite(settings.color) && settings.color.x >= 0 && settings.color.y >= 0 &&
               settings.color.z >= 0 && is_finite(settings.intensity) && settings.intensity >= 0;
    }

    bool is_valid(const LocalLightSettings& settings)
    {
        return is_finite(settings.range) && settings.range > 0;
    }

    bool is_valid(const DirectionalShadowSettings& settings)
    {
        return settings.cascade_count >= 1 && settings.cascade_count <= DirectionalShadowSettings::k_max_cascades &&
               is_finite(settings.distribution_exponent) && settings.distribution_exponent >= 0.1f &&
               settings.distribution_exponent <= 10.0f &&
               (settings.map_resolution == 512 || settings.map_resolution == 1024 || settings.map_resolution == 2048) &&
               is_finite(settings.distance) && settings.distance >= 0 &&
               is_finite(settings.fade_fraction) && settings.fade_fraction >= 0 && settings.fade_fraction < 1 &&
               is_finite(settings.bias) && settings.bias >= 0 && settings.bias <= 1 &&
               is_finite(settings.slope_bias) && settings.slope_bias >= 0 && settings.slope_bias <= 1 &&
               is_finite(settings.receiver_bias) && settings.receiver_bias >= 0 && settings.receiver_bias <= 1;
    }

    bool is_valid(const CameraSettings& settings)
    {
        if (!is_finite(settings.vertical_fov) || !is_finite(settings.near_clip) || !is_finite(settings.far_clip) ||
            settings.vertical_fov <= 0 || settings.vertical_fov >= 180 || settings.near_clip <= 0 ||
            settings.far_clip <= settings.near_clip) return false;
        PerspectiveProjectionDesc desc;
        desc.vertical_fov = to_radians(Degrees(settings.vertical_fov));
        desc.near_clip = settings.near_clip;
        desc.far_clip = settings.far_clip;
        Matrix4 projection;
        Matrix4 inverse;
        ConvexVolume frustum;
        return try_make_perspective_projection(desc, projection) && try_inverse(projection, inverse) &&
               try_make_reversed_z_frustum(projection, false, frustum);
    }
} // namespace toy3d
