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
        explicit ForwardSceneRenderer(SceneViewFamily view_family, bool thumbnail_preview = false,
                                      bool preview_shadows = false);
        ~ForwardSceneRenderer() override = default;

      private:
        RHIStatus render_scene_passes(RenderScene& render_scene, RHIDevice& device,
                                      RHIShaderProgramCache& shader_program_cache, RHIGraphicsCommandContext& context,
                                      SceneRenderTargets& scene_render_targets,
                                      const BuiltinMeshPassPrograms& mesh_pass_programs) override;
        RHIStatus render_hit_proxy(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                   const ShaderMapCollection& shader_map, RHIGraphicsCommandContext& context,
                                   const RHITextureViewRef& id_view, const RHITextureViewRef& depth_view,
                                   HitProxyTable& table) override;
        bool init_views();
        bool thumbnail_preview_ = false;
        bool preview_shadows_ = false;
    };
} // namespace toy3d
