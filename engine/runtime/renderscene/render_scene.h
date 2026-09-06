#pragma once

#include "rendercore/scene_interface.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class TaskGraphInterface;
    class ForwardSceneRenderer;
    class PrimitiveSceneInfo;
    class PrimitiveSceneProxy;
    class RenderResourceManager;

    // Renderer-owned Render-side scene. Mutable scene state and all future proxy
    // operations are restricted to the logical Rendering Thread and never read Game objects.
    class RenderScene final : public SceneInterface
    {
      public:
        RenderScene(TaskGraphInterface& task_graph, RenderResourceManager& resource_manager);
        ~RenderScene() override;

        RenderScene(const RenderScene&) = delete;
        RenderScene& operator=(const RenderScene&) = delete;
        RenderScene(RenderScene&&) = delete;
        RenderScene& operator=(RenderScene&&) = delete;

        void add_primitive(std::unique_ptr<PrimitiveSceneProxy> proxy) override;
        void update_primitive_transform(PrimitiveSceneProxy* proxy, Matrix4 world_transform,
                                        AxisAlignedBounds world_bounds, bool visible) override;
        void remove_primitive(PrimitiveSceneProxy* proxy) override;

      private:
        friend class ForwardSceneRenderer;

        const std::vector<std::unique_ptr<PrimitiveSceneInfo>>& primitive_scene_infos() const { return primitives_; }
        bool is_on_logical_rendering_thread() const;
        void add_primitive_render_thread(std::unique_ptr<PrimitiveSceneProxy> proxy) noexcept;
        void update_primitive_transform_render_thread(PrimitiveSceneProxy* proxy, Matrix4 world_transform,
                                                      AxisAlignedBounds world_bounds, bool visible) noexcept;
        void remove_primitive_render_thread(PrimitiveSceneProxy* proxy) noexcept;

        TaskGraphInterface& task_graph_;
        RenderResourceManager& resource_manager_;
        std::vector<std::unique_ptr<PrimitiveSceneInfo>> primitives_;
    };
} // namespace toy3d
