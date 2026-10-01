#pragma once

#include "rendercore/scene_interface.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class TaskGraphInterface;
    class ViewInfo;
    class PrimitiveSceneInfo;
    class PrimitiveSceneProxy;
    class RenderResourceManager;
    class RHIStatus;
    struct LightSceneData;

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
                                        AxisAlignedBounds world_bounds, bool visible,
                                        bool cast_shadows, bool receives_shadows) override;
        void update_primitive_materials(PrimitiveSceneProxy* proxy,
            std::vector<MaterialRenderProxy*> materials) override;
        void remove_primitive(PrimitiveSceneProxy* proxy) override;
        void add_light(std::unique_ptr<LightSceneProxy> proxy) override;
        void update_light(LightSceneProxy* proxy, LightSceneData data) override;
        void remove_light(LightSceneProxy* proxy) override;
        // Read only on the logical Rendering Thread while constructing forward pass parameters.
        const std::vector<std::unique_ptr<LightSceneProxy>>& lights() const;
        bool light_limit_reported() const { return light_limit_reported_; }
        void set_light_limit_reported(bool reported) { light_limit_reported_ = reported; }

      private:
        friend void compute_scene_visibility(const RenderScene& render_scene, std::vector<ViewInfo>& view_infos);
        friend RHIStatus compute_shadow_visibility(const RenderScene& render_scene,
                                                   const LightSceneData* directional_light,
                                                   std::vector<ViewInfo>& view_infos, std::uint32_t shadow_resolution);

        const std::vector<std::unique_ptr<PrimitiveSceneInfo>>& primitive_scene_infos() const { return primitives_; }
        bool is_on_logical_rendering_thread() const;
        void add_primitive_render_thread(std::unique_ptr<PrimitiveSceneProxy> proxy) noexcept;
        void update_primitive_transform_render_thread(PrimitiveSceneProxy* proxy, Matrix4 world_transform,
                                                      AxisAlignedBounds world_bounds, bool visible,
                                                      bool cast_shadows, bool receives_shadows) noexcept;
        void remove_primitive_render_thread(PrimitiveSceneProxy* proxy) noexcept;

        TaskGraphInterface& task_graph_;
        RenderResourceManager& resource_manager_;
        std::vector<std::unique_ptr<PrimitiveSceneInfo>> primitives_;
        std::vector<std::unique_ptr<LightSceneProxy>> lights_;
        bool light_limit_reported_ = false;
    };
} // namespace toy3d
