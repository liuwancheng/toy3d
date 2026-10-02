#pragma once

#include "math/matrix4.h"
#include "rendercore/geometry/axis_aligned_bounds.h"
#include "rendercore/scene/light_scene_proxy.h"

#include <memory>
#include <atomic>
#include <string>
#include <vector>

namespace toy3d
{
    enum class SceneRenderState
    {
        Pending,
        Ready,
        Failed
    };

    // One preparation attempt. RT writes error before release-publishing Failed;
    // GT reads it only after acquiring a completed state. Ready means a submitted
    // scene frame, not GPU completion. Shared ownership retains in-flight feedback.
    struct SceneRenderFeedback
    {
        std::atomic<SceneRenderState> state{SceneRenderState::Pending};
        std::string error;
    };

    struct SkeletalMeshDeformationData;
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
        virtual void update_skeletal_mesh_pose(PrimitiveSceneProxy* proxy,
                                               std::shared_ptr<const SkeletalMeshDeformationData> deformation,
                                               Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                               bool cast_shadows, bool receives_shadows) = 0;
        virtual void remove_primitive(PrimitiveSceneProxy* proxy) = 0;
        virtual void add_light(std::unique_ptr<LightSceneProxy> proxy) = 0;
        virtual void update_light(LightSceneProxy* proxy, LightSceneData data) = 0;
        virtual void remove_light(LightSceneProxy* proxy) = 0;
    };

    inline SceneInterface::~SceneInterface() = default;
} // namespace toy3d
