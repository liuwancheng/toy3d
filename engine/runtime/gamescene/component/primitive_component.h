#pragma once

#include "gamescene/component/scene_component.h"
#include "rendercore/geometry/axis_aligned_bounds.h"

namespace toy3d
{
    class PrimitiveComponent : public SceneComponent
    {
    public:
        ~PrimitiveComponent() override = default;

        const AxisAlignedBounds& world_bounds() const { return world_bounds_; }

    protected:
        explicit PrimitiveComponent(Actor& owner) : SceneComponent(owner) {}

        void on_world_transform_updated() override { update_bounds(); }
        virtual void update_bounds() = 0;

        AxisAlignedBounds world_bounds_;
    };
}
