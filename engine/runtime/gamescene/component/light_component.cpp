#include "gamescene/component/light_component.h"

#include "logging/logger.h"

#include <cmath>

namespace toy3d
{
    namespace
    {
        bool is_finite(const vec3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }
    }

    bool LightComponent::set_color(const vec3& color)
    {
        if (!is_finite(color) || color.x < 0.0f || color.y < 0.0f || color.z < 0.0f)
        {
            TOY_LOG_ERROR("Light color must be finite and non-negative linear RGB.");
            return false;
        }
        color_ = color;
        return true;
    }

    bool LightComponent::set_intensity(float intensity)
    {
        if (!std::isfinite(intensity) || intensity < 0.0f)
        {
            TOY_LOG_ERROR("Light intensity must be finite and non-negative.");
            return false;
        }
        intensity_ = intensity;
        return true;
    }

    bool LocalLightComponent::set_range(float range)
    {
        if (!std::isfinite(range) || range <= 0.0f)
        {
            TOY_LOG_ERROR("A local light range must be finite and greater than zero.");
            return false;
        }
        range_ = range;
        return true;
    }

    bool SpotLightComponent::set_cone_angles(
        float inner_angle_degrees,
        float outer_angle_degrees)
    {
        if (!std::isfinite(inner_angle_degrees) ||
            !std::isfinite(outer_angle_degrees) ||
            inner_angle_degrees < 0.0f ||
            inner_angle_degrees > outer_angle_degrees ||
            outer_angle_degrees >= 90.0f)
        {
            TOY_LOG_ERROR(
                "A SpotLight requires 0 <= inner angle <= outer angle < 90 degrees.");
            return false;
        }
        inner_angle_degrees_ = inner_angle_degrees;
        outer_angle_degrees_ = outer_angle_degrees;
        return true;
    }
}
