#pragma once

#include "math/matrix4.h"
#include "math/quaternion.h"
#include "rendercore/texture/texture.h"
#include "rendercore/geometry/axis_aligned_bounds.h"
#include "rendercore/scene/light_scene_proxy.h"

#include <memory>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

namespace toy3d
{
    struct SceneEnvironmentSnapshot
    {
        TextureRef cube;
        Quaternion rotation;
        float intensity = 1.0f;
    };
    bool validate_scene_environment_snapshot(const SceneEnvironmentSnapshot& snapshot, std::string& error);

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

    // Bridge from Game-side World code to Render-side scene commands. Its owned
    // admission generation lets GT reject geometry/users changed during preflight.
    // Domain lifecycle operations must remain one-way and never expose RenderScene,
    // Renderer, RHI, or backend state to Game-side callers.
    class SceneInterface
    {
      public:
        SceneInterface() = default;
        virtual ~SceneInterface() = 0;

        SceneInterface(const SceneInterface&) = delete;
        SceneInterface& operator=(const SceneInterface&) = delete;
        std::shared_ptr<const std::atomic<std::uint64_t>> material_usage_generation() const
        {
            return material_usage_generation_;
        }

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
        virtual void update_environment(SceneEnvironmentSnapshot environment) = 0;
        virtual void add_light(std::unique_ptr<LightSceneProxy> proxy) = 0;
        virtual void update_light(LightSceneProxy* proxy, LightSceneData data) = 0;
        virtual void remove_light(LightSceneProxy* proxy) = 0;

      protected:
        void mark_material_usage_changed()
        {
            material_usage_generation_->fetch_add(1u, std::memory_order_release);
        }

      private:
        // Completion requests may outlive the scene domain; they own this small
        // revision counter, never the scene or any GT object graph.
        std::shared_ptr<std::atomic<std::uint64_t>> material_usage_generation_ =
            std::make_shared<std::atomic<std::uint64_t>>(0u);
    };

    inline SceneInterface::~SceneInterface() = default;
} // namespace toy3d
