#pragma once

#include "gamescene/component/scene_component.h"
#include "rendercore/geometry/axis_aligned_bounds.h"

#include <memory>

namespace toy3d
{
    class PrimitiveSceneProxy;

    class PrimitiveComponent : public SceneComponent
    {
      public:
        ~PrimitiveComponent() override;

        const AxisAlignedBounds& world_bounds() const { return world_bounds_; }
        bool visible() const { return visible_; }
        void set_visible(bool visible);
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
        virtual std::unique_ptr<PrimitiveSceneProxy> create_scene_proxy() const = 0;

        AxisAlignedBounds world_bounds_;

      private:
        PrimitiveSceneProxy* scene_proxy_ = nullptr;
        bool visible_ = true;
    };
} // namespace toy3d
