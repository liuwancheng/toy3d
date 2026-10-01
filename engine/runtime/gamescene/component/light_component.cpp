#include "gamescene/component/light_component.h"

#include "logging/logger.h"
#include "math/scalar_math.h"
#include "gamescene/world/world.h"
#include "rendercore/scene_interface.h"
#include <cassert>
#include <utility>

namespace toy3d
{
    // --------------------------------------------------------------------------
    // LightComponent: validates GT properties and owns the opaque render-state identity
    // --------------------------------------------------------------------------
    LightComponent::~LightComponent()
    {
        // Actor unregister / World unbind must run while SceneInterface is still alive.
        assert(scene_proxy_ == nullptr);
    }

    void LightComponent::set_enabled(bool enabled)
    {
        enabled_ = enabled;
        send_render_update();
    }

    bool LightComponent::set_color(const Vector3& color)
    {
        if (!is_finite(color) || color.x < 0.0f || color.y < 0.0f || color.z < 0.0f)
        {
            TOY_LOG_ERROR("Light color must be finite and non-negative linear RGB.");
            return false;
        }
        color_ = color;
        send_render_update();
        return true;
    }

    bool LightComponent::set_intensity(float intensity)
    {
        if (!is_finite(intensity) || intensity < 0.0f)
        {
            TOY_LOG_ERROR("Light intensity must be finite and non-negative.");
            return false;
        }
        intensity_ = intensity;
        send_render_update();
        return true;
    }

    void LightComponent::set_render_priority(int render_priority)
    {
        render_priority_ = render_priority;
        send_render_update();
    }


    LightSceneData LightComponent::scene_data() const
    {
        LightSceneData data;
        data.kind = dynamic_cast<const PointLightComponent*>(this) ? LightKind::Point : LightKind::Directional;
        data.position = transform_position(world_transform(), Vector3());
        data.direction = rotate_vector(world_rotation(), Vector3(0, 0, 1));
        data.color = color_;
        data.intensity = intensity_;
        data.enabled = enabled_;
        data.priority = render_priority_;
        if (const auto* local = dynamic_cast<const LocalLightComponent*>(this)) data.range = local->range();
        if (const auto* directional = dynamic_cast<const DirectionalLightComponent*>(this))
        {
            data.cast_shadows = directional->cast_shadows();
            data.shadow_cascade_count = directional->shadow_cascade_count();
            data.cascade_distribution_exponent = directional->cascade_distribution_exponent();
            data.shadow_map_resolution = directional->shadow_map_resolution();
            data.shadow_distance = directional->shadow_distance();
            data.shadow_distance_fade_fraction = directional->shadow_distance_fade_fraction();
            data.shadow_bias = directional->shadow_bias();
            data.shadow_slope_bias = directional->shadow_slope_bias();
            data.shadow_receiver_bias = directional->shadow_receiver_bias();
        }
        return data;
    }

    void LightComponent::create_render_state()
    {
        if (scene_proxy_ || !is_registered() || !world().scene_interface()) return;
        if (dynamic_cast<SpotLightComponent*>(this))
        {
            TOY_LOG_ERROR("Spot light rendering is not supported yet.");
            return;
        }
        auto proxy = std::make_unique<LightSceneProxy>();
        proxy->data = scene_data();
        LightSceneProxy* identity = proxy.get();
        world().scene_interface()->add_light(std::move(proxy));
        scene_proxy_ = identity;
        world().mark_scene_changed();
    }

    void LightComponent::destroy_render_state()
    {
        if (!scene_proxy_) return;
        assert(world().scene_interface());
        world().scene_interface()->remove_light(scene_proxy_);
        scene_proxy_ = nullptr;
        world().mark_scene_changed();
    }

    void LightComponent::send_render_update()
    {
        if (!scene_proxy_ || !world().scene_interface()) return;
        world().scene_interface()->update_light(scene_proxy_, scene_data());
        world().mark_scene_changed();
    }

    void LightComponent::on_register() { create_render_state(); }
    void LightComponent::on_unregister() { destroy_render_state(); }
    void LightComponent::on_world_transform_updated() { send_render_update(); }

    // --------------------------------------------------------------------------
    // DirectionalLightComponent: owns validated per-light shadow settings
    // --------------------------------------------------------------------------
    void DirectionalLightComponent::set_cast_shadows(bool enabled)
    {
        cast_shadows_ = enabled;
        send_render_update();
    }

    bool DirectionalLightComponent::set_shadow_cascade_count(int count)
    {
        if (count < 1 || count > LightSceneData::k_max_shadow_cascades)
        {
            TOY_LOG_ERROR("Directional shadow cascade count must be in [1, {}].", LightSceneData::k_max_shadow_cascades);
            return false;
        }
        shadow_cascade_count_ = count;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_cascade_distribution_exponent(float exponent)
    {
        if (!is_finite(exponent) || exponent < 0.1f || exponent > 10.0f)
        {
            TOY_LOG_ERROR("Cascade distribution exponent must be finite and in [0.1, 10].");
            return false;
        }
        cascade_distribution_exponent_ = exponent;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_shadow_map_resolution(int resolution)
    {
        if (resolution < LightSceneData::k_min_shadow_resolution ||
            resolution > LightSceneData::k_max_shadow_resolution || (resolution & (resolution - 1)) != 0)
        {
            TOY_LOG_ERROR("Shadow maximum resolution must be 512, 1024 or 2048.");
            return false;
        }
        shadow_map_resolution_ = resolution;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_shadow_distance(float distance)
    {
        if (!is_finite(distance) || distance < 0.0f)
        {
            TOY_LOG_ERROR("Directional shadow distance must be finite and non-negative.");
            return false;
        }
        shadow_distance_ = distance;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_shadow_distance_fade_fraction(float fraction)
    {
        if (!is_finite(fraction) || fraction < 0.0f || fraction >= 1.0f)
        {
            TOY_LOG_ERROR("Directional shadow fade fraction must be in [0, 1).");
            return false;
        }
        shadow_distance_fade_fraction_ = fraction;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_shadow_bias(float bias)
    {
        if (!is_finite(bias) || bias < 0.0f || bias > 1.0f)
        {
            TOY_LOG_ERROR("Directional shadow bias must be in [0, 1].");
            return false;
        }
        shadow_bias_ = bias;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_shadow_slope_bias(float bias)
    {
        if (!is_finite(bias) || bias < 0.0f || bias > 1.0f)
        {
            TOY_LOG_ERROR("Directional shadow slope bias must be in [0, 1].");
            return false;
        }
        shadow_slope_bias_ = bias;
        send_render_update();
        return true;
    }

    bool DirectionalLightComponent::set_shadow_receiver_bias(float bias)
    {
        if (!is_finite(bias) || bias < 0.0f || bias > 1.0f)
        {
            TOY_LOG_ERROR("Directional shadow receiver bias must be in [0, 1].");
            return false;
        }
        shadow_receiver_bias_ = bias;
        send_render_update();
        return true;
    }

    // --------------------------------------------------------------------------
    // LocalLightComponent: validates the finite attenuation radius
    // --------------------------------------------------------------------------
    bool LocalLightComponent::set_range(float range)
    {
        if (!is_finite(range) || range <= 0.0f)
        {
            TOY_LOG_ERROR("A local light range must be finite and greater than zero.");
            return false;
        }
        range_ = range;
        send_render_update();
        return true;
    }

    // --------------------------------------------------------------------------
    // SpotLightComponent: stores validated cone angles; rendering is not yet supported
    // --------------------------------------------------------------------------
    bool SpotLightComponent::set_cone_angles(float inner_angle_degrees, float outer_angle_degrees)
    {
        if (!is_finite(inner_angle_degrees) || !is_finite(outer_angle_degrees) || inner_angle_degrees < 0.0f ||
            inner_angle_degrees > outer_angle_degrees || outer_angle_degrees >= 90.0f)
        {
            TOY_LOG_ERROR("A SpotLight requires 0 <= inner angle <= outer angle < 90 degrees.");
            return false;
        }
        inner_angle_degrees_ = inner_angle_degrees;
        outer_angle_degrees_ = outer_angle_degrees;
        return true;
    }
} // namespace toy3d
