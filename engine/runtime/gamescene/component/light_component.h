#pragma once

#include "gamescene/component/scene_component.h"

namespace toy3d
{
    class LightComponent : public SceneComponent
    {
    public:
        explicit LightComponent(Actor& owner) : SceneComponent(owner) {}
        ~LightComponent() override = default;

        bool enabled() const { return enabled_; }
        void set_enabled(bool enabled) { enabled_ = enabled; }

        const vec3& color() const { return color_; }
        bool set_color(const vec3& color);

        float intensity() const { return intensity_; }
        bool set_intensity(float intensity);

        int render_priority() const { return render_priority_; }
        void set_render_priority(int render_priority) { render_priority_ = render_priority; }

    private:
        bool enabled_ = true;
        vec3 color_{1.0f};
        float intensity_ = 1.0f;
        int render_priority_ = 0;
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
        explicit LocalLightComponent(Actor& owner) : LightComponent(owner) {}
        ~LocalLightComponent() override = default;

        float range() const { return range_; }
        bool set_range(float range);

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
}
