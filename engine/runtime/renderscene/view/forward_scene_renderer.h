#pragma once

#include "renderscene/view/scene_renderer.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIStatus;
    class SceneRenderTargets;
    struct RHIRenderPassDesc;

    class ForwardSceneRenderer final : public SceneRenderer
    {
    public:
        explicit ForwardSceneRenderer(SceneViewFamily view_family);
        ~ForwardSceneRenderer() override = default;

    private:
        struct PreparedBasePass;

        RHIStatus render_scene_passes(
            RenderScene& render_scene,
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            SceneRenderTargets& scene_render_targets) override;
        bool init_views();
        void compute_view_visibility(const RenderScene& render_scene);
        void collect_mesh_batches();
        RHIStatus prepare_base_pass(
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            const RHIRenderPassDesc& pass_desc,
            PreparedBasePass& prepared_pass);
        RHIStatus execute_base_pass(
            RHIGraphicsCommandContext& context,
            const PreparedBasePass& prepared_pass);
    };
}
