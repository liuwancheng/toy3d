#pragma once

#include "gamescene/component/scene_component.h"
#include "scene_data/component_settings.h"
#include "rendercore/geometry/axis_aligned_bounds.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class PrimitiveSceneProxy;
    class MaterialRenderProxy;

    class PrimitiveComponent : public SceneComponent
    {
      public:
        ~PrimitiveComponent() override;

        const AxisAlignedBounds& world_bounds() const { return world_bounds_; }
        const PrimitiveSettings& primitive_settings() const { return settings_; }
        void set_primitive_settings(const PrimitiveSettings& settings);

        bool visible() const { return settings_.visible; }
        void set_visible(bool visible);
        bool cast_shadows() const { return settings_.cast_shadows; }
        void set_cast_shadows(bool cast_shadows);
        bool receives_shadows() const { return settings_.receives_shadows; }
        void set_receives_shadows(bool receives_shadows);
        bool has_render_state() const { return scene_proxy_ != nullptr; }

        void create_render_state();
        void send_render_transform();
        void destroy_render_state();

      protected:
        explicit PrimitiveComponent(Actor& owner) : SceneComponent(owner) {}

        void on_register() override;
        void on_unregister() override;
        void on_world_transform_updated() override;
        virtual void update_bounds() = 0;
        void send_render_materials(std::vector<MaterialRenderProxy*> materials);
        virtual void on_render_state_removed() {}
        virtual std::unique_ptr<PrimitiveSceneProxy> create_scene_proxy() const = 0;

        AxisAlignedBounds world_bounds_;

      private:
        PrimitiveSceneProxy* scene_proxy_ = nullptr;
        PrimitiveSettings settings_;
    };
} // namespace toy3d
