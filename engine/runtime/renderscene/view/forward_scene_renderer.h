#pragma once

#include "renderscene/view/scene_renderer.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIStatus;
    class RenderResourceManager;
    struct RHIRenderPassDesc;

    class ForwardSceneRenderer final : public SceneRenderer
    {
    public:
        explicit ForwardSceneRenderer(SceneViewFamily view_family);
        ~ForwardSceneRenderer() override = default;

        // Render-side frame owner. Renderer supplies borrowed domain objects;
        // primary viewport and depth-resource ownership remain outside this
        // one-shot SceneRenderer. The injected depth view must already be
        // committed or current-list eligible for DepthStencilWrite access.
        RHIResult<RHIFrameEndResult> render_frame(
            RenderScene& render_scene,
            RHIDevice& device,
            RenderResourceManager& resource_manager,
            RHIViewportContext& viewport,
            const RHITextureViewRef& depth_stencil_view) override;

    private:
        bool init_views();
        void compute_view_visibility(const RenderScene& render_scene);
        void collect_mesh_batches();
        RHIStatus render_base_pass(
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            const RHIRenderPassDesc& pass_desc);
        void render(RenderScene& render_scene) noexcept override;
    };
}
