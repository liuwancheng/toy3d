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
    bool LightComponent::set_light_settings(const LightSettings& settings)
    {
        if (!is_valid(settings))
        {
            TOY_LOG_ERROR("Invalid LightSettings.");
            return false;
        }
        if (light_settings_ == settings) return true;
        light_settings_ = settings;
        world().mark_content_changed();
        send_render_update();
        return true;
    }

    LightComponent::~LightComponent()
    {
        // Actor unregister / World unbind must run while SceneInterface is still alive.
        assert(scene_proxy_ == nullptr);
    }

    void LightComponent::set_enabled(bool enabled)
    {
        LightSettings candidate = light_settings_;
        candidate.enabled = enabled;
        if (!set_light_settings(candidate)) TOY_LOG_ERROR("Light edit rejected invalid settings.");
    }

    bool LightComponent::set_color(const Vector3& color)
    {
        LightSettings candidate = light_settings_;
        candidate.color = color;
        return set_light_settings(candidate);
    }

    bool LightComponent::set_intensity(float intensity)
    {
        LightSettings candidate = light_settings_;
        candidate.intensity = intensity;
        return set_light_settings(candidate);
    }

    void LightComponent::set_render_priority(int render_priority)
    {
        LightSettings candidate = light_settings_;
        candidate.priority = render_priority;
        if (!set_light_settings(candidate)) TOY_LOG_ERROR("Light edit rejected invalid settings.");
    }


    LightSceneData LightComponent::scene_data() const
    {
        LightSceneData data;
        data.kind = dynamic_cast<const PointLightComponent*>(this) ? LightKind::Point : LightKind::Directional;
        data.position = transform_position(world_transform(), Vector3());
        data.direction = rotate_vector(world_rotation(), Vector3(0, 0, 1));
        data.color = light_settings_.color;
        data.intensity = light_settings_.intensity;
        data.enabled = light_settings_.enabled;
        data.priority = light_settings_.priority;
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
    bool DirectionalLightComponent::set_shadow_settings(const DirectionalShadowSettings& settings)
    {
        if (!is_valid(settings))
        {
            TOY_LOG_ERROR("Invalid DirectionalShadowSettings.");
            return false;
        }
        if (shadow_settings_ == settings) return true;
        shadow_settings_ = settings;
        world().mark_content_changed();
        send_render_update();
        return true;
    }

    void DirectionalLightComponent::set_cast_shadows(bool enabled)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.cast_shadows = enabled;
        if (!set_shadow_settings(candidate)) TOY_LOG_ERROR("Light edit rejected invalid settings.");
    }

    bool DirectionalLightComponent::set_shadow_cascade_count(int count)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.cascade_count = count;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_cascade_distribution_exponent(float exponent)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.distribution_exponent = exponent;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_shadow_map_resolution(int resolution)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.map_resolution = resolution;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_shadow_distance(float distance)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.distance = distance;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_shadow_distance_fade_fraction(float fraction)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.fade_fraction = fraction;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_shadow_bias(float bias)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.bias = bias;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_shadow_slope_bias(float bias)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.slope_bias = bias;
        return set_shadow_settings(candidate);
    }

    bool DirectionalLightComponent::set_shadow_receiver_bias(float bias)
    {
        DirectionalShadowSettings candidate = shadow_settings_;
        candidate.receiver_bias = bias;
        return set_shadow_settings(candidate);
    }

    // --------------------------------------------------------------------------
    // LocalLightComponent: validates the finite attenuation radius
    // --------------------------------------------------------------------------
    bool LocalLightComponent::set_local_light_settings(const LocalLightSettings& settings)
    {
        if (!is_valid(settings))
        {
            TOY_LOG_ERROR("Invalid LocalLightSettings.");
            return false;
        }
        if (local_settings_ == settings) return true;
        local_settings_ = settings;
        world().mark_content_changed();
        send_render_update();
        return true;
    }

    bool LocalLightComponent::set_range(float range)
    {
        LocalLightSettings candidate = local_settings_;
        candidate.range = range;
        return set_local_light_settings(candidate);
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
