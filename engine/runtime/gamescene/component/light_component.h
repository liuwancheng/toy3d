#pragma once

#include "gamescene/component/scene_component.h"
#include "asset/scene/component_settings.h"
#include "math/vector3.h"
#include "rendercore/scene/light_scene_proxy.h"

namespace toy3d
{
    class LightComponent : public SceneComponent
    {
      public:
        ~LightComponent() override;

        bool enabled() const
        {
            return light_settings_.enabled;
        }
        void set_enabled(bool enabled);

        const Vector3& color() const
        {
            return light_settings_.color;
        }
        bool set_color(const Vector3& color);

        float intensity() const
        {
            return light_settings_.intensity;
        }
        bool set_intensity(float intensity);

        int render_priority() const
        {
            return light_settings_.priority;
        }
        void set_render_priority(int render_priority);

        const LightSettings& light_settings() const
        {
            return light_settings_;
        }
        bool set_light_settings(const LightSettings& settings);

        void create_render_state();
        void destroy_render_state();

      protected:
        explicit LightComponent(Actor& owner) : SceneComponent(owner)
        {
        }
        void on_register() override;
        void on_unregister() override;
        void on_world_transform_updated() override;
        void send_render_update();

      private:
        LightSettings light_settings_;
        LightSceneData scene_data() const;
        LightSceneProxy* scene_proxy_ = nullptr;
    };

    class DirectionalLightComponent final : public LightComponent
    {
      public:
        explicit DirectionalLightComponent(Actor& owner) : LightComponent(owner)
        {
        }
        ~DirectionalLightComponent() override = default;

        const DirectionalShadowSettings& shadow_settings() const
        {
            return shadow_settings_;
        }
        bool set_shadow_settings(const DirectionalShadowSettings& settings);

        bool cast_shadows() const
        {
            return shadow_settings_.cast_shadows;
        }
        void set_cast_shadows(bool enabled);
        int shadow_cascade_count() const
        {
            return shadow_settings_.cascade_count;
        }
        bool set_shadow_cascade_count(int count);
        float cascade_distribution_exponent() const
        {
            return shadow_settings_.distribution_exponent;
        }
        bool set_cascade_distribution_exponent(float exponent);
        int shadow_map_resolution() const
        {
            return shadow_settings_.map_resolution;
        }
        bool set_shadow_map_resolution(int resolution);
        float shadow_distance() const
        {
            return shadow_settings_.distance;
        }
        bool set_shadow_distance(float distance);
        float shadow_distance_fade_fraction() const
        {
            return shadow_settings_.fade_fraction;
        }
        bool set_shadow_distance_fade_fraction(float fraction);
        float shadow_bias() const
        {
            return shadow_settings_.bias;
        }
        bool set_shadow_bias(float bias);
        float shadow_slope_bias() const
        {
            return shadow_settings_.slope_bias;
        }
        bool set_shadow_slope_bias(float bias);
        float shadow_receiver_bias() const
        {
            return shadow_settings_.receiver_bias;
        }
        bool set_shadow_receiver_bias(float bias);

      private:
        DirectionalShadowSettings shadow_settings_;
    };

    class LocalLightComponent : public LightComponent
    {
      public:
        ~LocalLightComponent() override = default;

        const LocalLightSettings& local_light_settings() const
        {
            return local_settings_;
        }
        bool set_local_light_settings(const LocalLightSettings& settings);

        float range() const
        {
            return local_settings_.range;
        }
        bool set_range(float range);

      protected:
        explicit LocalLightComponent(Actor& owner) : LightComponent(owner)
        {
        }

      private:
        LocalLightSettings local_settings_;
    };

    class PointLightComponent final : public LocalLightComponent
    {
      public:
        explicit PointLightComponent(Actor& owner) : LocalLightComponent(owner)
        {
        }
        ~PointLightComponent() override = default;
    };

    class SpotLightComponent final : public LocalLightComponent
    {
      public:
        explicit SpotLightComponent(Actor& owner) : LocalLightComponent(owner)
        {
        }
        ~SpotLightComponent() override = default;

        float inner_angle_degrees() const
        {
            return inner_angle_degrees_;
        }
        float outer_angle_degrees() const
        {
            return outer_angle_degrees_;
        }
        bool set_cone_angles(float inner_angle_degrees, float outer_angle_degrees);

      private:
        float inner_angle_degrees_ = 20.0f;
        float outer_angle_degrees_ = 30.0f;
    };
} // namespace toy3d
