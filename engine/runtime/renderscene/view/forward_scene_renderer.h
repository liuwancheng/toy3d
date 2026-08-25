#pragma once

#include "renderscene/view/scene_renderer.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIStatus;
    class RenderResourceManager;
    class SceneRenderTargets;
    struct RHIRenderPassDesc;

    class ForwardSceneRenderer final : public SceneRenderer
    {
    public:
        explicit ForwardSceneRenderer(SceneViewFamily view_family);
        ~ForwardSceneRenderer() override = default;

        // Render-side frame owner. Renderer supplies borrowed domain objects;
        // presentation ownership remains in the viewport, while scene
        // attachments are held by Renderer-owned SceneRenderTargets.
        RHIResult<RHIFrameEndResult> render_frame(
            RenderScene& render_scene,
            RHIDevice& device,
            RenderResourceManager& resource_manager,
            RHIViewportContext& viewport,
            SceneRenderTargets& scene_render_targets) override;

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
