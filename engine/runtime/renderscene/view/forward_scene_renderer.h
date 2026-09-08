#pragma once

#include "renderscene/view/scene_renderer.h"

namespace toy3d
{
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIShaderProgramCache;
    class RHIStatus;
    class SceneRenderTargets;

    class ForwardSceneRenderer final : public SceneRenderer
    {
      public:
        explicit ForwardSceneRenderer(SceneViewFamily view_family);
        ~ForwardSceneRenderer() override = default;

      private:
        RHIStatus render_scene_passes(RenderScene& render_scene, RHIDevice& device,
                                      RHIShaderProgramCache& shader_program_cache, RHIGraphicsCommandContext& context,
                                      SceneRenderTargets& scene_render_targets) override;
        bool init_views();
    };
} // namespace toy3d
