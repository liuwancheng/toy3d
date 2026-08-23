#pragma once

#include "math/matrix4.h"
#include "rendercore/geometry/axis_aligned_bounds.h"

namespace toy3d
{
    class RenderScene;

    // Render-side representation with a stable address. Game-side code may retain
    // its pointer only as an opaque identity protected by RenderCommand FIFO order.
    class PrimitiveSceneProxy
    {
    public:
        virtual ~PrimitiveSceneProxy() = default;

        PrimitiveSceneProxy(const PrimitiveSceneProxy&) = delete;
        PrimitiveSceneProxy& operator=(const PrimitiveSceneProxy&) = delete;

        const Matrix4& world_transform() const { return world_transform_; }
        const AxisAlignedBounds& world_bounds() const { return world_bounds_; }
        bool visible() const { return visible_; }

    protected:
        PrimitiveSceneProxy(
            Matrix4 world_transform,
            AxisAlignedBounds world_bounds,
            bool visible);

    private:
        friend class RenderScene;

        void update_transform(
            Matrix4 world_transform,
            AxisAlignedBounds world_bounds,
            bool visible);

        Matrix4 world_transform_;
        AxisAlignedBounds world_bounds_;
        bool visible_ = true;
    };
}
