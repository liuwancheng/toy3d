#pragma once

#include "math/matrix4.h"
#include "rendercore/geometry/axis_aligned_bounds.h"
#include "rendercore/scene/light_scene_proxy.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class PrimitiveSceneProxy;
    class MaterialRenderProxy;

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

        virtual void add_primitive(std::unique_ptr<PrimitiveSceneProxy> proxy) = 0;
        virtual void update_primitive_transform(PrimitiveSceneProxy* proxy, Matrix4 world_transform,
                                                AxisAlignedBounds world_bounds, bool visible, bool cast_shadows,
                                                bool receives_shadows) = 0;
        virtual void update_primitive_materials(PrimitiveSceneProxy* proxy,
                                                std::vector<MaterialRenderProxy*> materials) = 0;
        virtual void remove_primitive(PrimitiveSceneProxy* proxy) = 0;
        virtual void add_light(std::unique_ptr<LightSceneProxy> proxy) = 0;
        virtual void update_light(LightSceneProxy* proxy, LightSceneData data) = 0;
        virtual void remove_light(LightSceneProxy* proxy) = 0;
    };

    inline SceneInterface::~SceneInterface() = default;
} // namespace toy3d
