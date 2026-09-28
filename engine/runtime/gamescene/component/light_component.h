#pragma once

#include "gamescene/component/scene_component.h"
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
        float range_ = 10.0f;
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
