#pragma once

#include "gamescene/component/scene_component.h"
#include "math/length_units.h"
#include "math/vector3.h"
#include "rendercore/scene/light_scene_proxy.h"

namespace toy3d
{
    class LightComponent : public SceneComponent
    {
      public:
        ~LightComponent() override;

        bool enabled() const { return enabled_; }
        void set_enabled(bool enabled);

        const Vector3& color() const { return color_; }
        bool set_color(const Vector3& color);

        float intensity() const { return intensity_; }
        bool set_intensity(float intensity);

        int render_priority() const { return render_priority_; }
        void set_render_priority(int render_priority);

        void create_render_state();
        void destroy_render_state();

      protected:
        explicit LightComponent(Actor& owner) : SceneComponent(owner) {}
        void on_register() override;
        void on_unregister() override;
        void on_world_transform_updated() override;
        void send_render_update();

      private:
        bool enabled_ = true;
        Vector3 color_{1.0f};
        float intensity_ = 1.0f;
        int render_priority_ = 0;
        LightSceneData scene_data() const;
        LightSceneProxy* scene_proxy_ = nullptr;
    };

    class DirectionalLightComponent final : public LightComponent
    {
      public:
        explicit DirectionalLightComponent(Actor& owner) : LightComponent(owner) {}
        ~DirectionalLightComponent() override = default;

        bool cast_shadows() const { return cast_shadows_; }
        void set_cast_shadows(bool enabled);
        int shadow_cascade_count() const { return shadow_cascade_count_; }
        bool set_shadow_cascade_count(int count);
        float cascade_distribution_exponent() const { return cascade_distribution_exponent_; }
        bool set_cascade_distribution_exponent(float exponent);
        int shadow_map_resolution() const { return shadow_map_resolution_; }
        bool set_shadow_map_resolution(int resolution);
        float shadow_distance() const { return shadow_distance_; }
        bool set_shadow_distance(float distance);
        float shadow_distance_fade_fraction() const { return shadow_distance_fade_fraction_; }
        bool set_shadow_distance_fade_fraction(float fraction);
        float shadow_bias() const { return shadow_bias_; }
        bool set_shadow_bias(float bias);
        float shadow_slope_bias() const { return shadow_slope_bias_; }
        bool set_shadow_slope_bias(float bias);
        float shadow_receiver_bias() const { return shadow_receiver_bias_; }
        bool set_shadow_receiver_bias(float bias);

      private:
        bool cast_shadows_ = false;
        int shadow_cascade_count_ = 1;
        float cascade_distribution_exponent_ = 3.0f;
        int shadow_map_resolution_ = LightSceneData::k_default_shadow_resolution;
        float shadow_distance_ = meters_to_centimeters(100.0f);
        float shadow_distance_fade_fraction_ = 0.1f;
        float shadow_bias_ = 0.5f;
        float shadow_slope_bias_ = 0.5f;
        float shadow_receiver_bias_ = 0.9f;
    };

    class LocalLightComponent : public LightComponent
    {
      public:
        ~LocalLightComponent() override = default;

        float range() const { return range_; }
        bool set_range(float range);

      protected:
        explicit LocalLightComponent(Actor& owner) : LightComponent(owner) {}

      private:
        float range_ = meters_to_centimeters(10.0f);
    };

    class PointLightComponent final : public LocalLightComponent
    {
      public:
        explicit PointLightComponent(Actor& owner) : LocalLightComponent(owner) {}
        ~PointLightComponent() override = default;
    };

    class SpotLightComponent final : public LocalLightComponent
    {
      public:
        explicit SpotLightComponent(Actor& owner) : LocalLightComponent(owner) {}
        ~SpotLightComponent() override = default;

        float inner_angle_degrees() const { return inner_angle_degrees_; }
        float outer_angle_degrees() const { return outer_angle_degrees_; }
        bool set_cone_angles(float inner_angle_degrees, float outer_angle_degrees);

      private:
        float inner_angle_degrees_ = 20.0f;
        float outer_angle_degrees_ = 30.0f;
    };
} // namespace toy3d
