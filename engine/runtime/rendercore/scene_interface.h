#pragma once

#include "math/matrix4.h"
#include "rendercore/geometry/axis_aligned_bounds.h"

#include <memory>

namespace toy3d
{
    class PrimitiveSceneProxy;

    // Stateless bridge from Game-side World code to Render-side scene commands.
    // Domain lifecycle operations must remain one-way and never expose RenderScene,
    // Renderer, RHI, or backend state to Game-side callers.
    class SceneInterface
    {
    public:
        SceneInterface() = default;
        virtual ~SceneInterface() = 0;

        SceneInterface(const SceneInterface&) = delete;
        SceneInterface& operator=(const SceneInterface&) = delete;

        virtual void add_primitive(
            std::unique_ptr<PrimitiveSceneProxy> proxy) = 0;
        virtual void update_primitive_transform(
            PrimitiveSceneProxy* proxy,
            Matrix4 world_transform,
            AxisAlignedBounds world_bounds,
            bool visible) = 0;
        virtual void remove_primitive(PrimitiveSceneProxy* proxy) = 0;
    };

    inline SceneInterface::~SceneInterface() = default;
}
