#pragma once

#include "drivers/rhi/rhi_result.h"
#include "rendercore/view/scene_view.h"
#include "rendercore/hit_proxy.h"
#include "renderscene/view/view_info.h"

#include <vector>

namespace toy3d
{
    class RenderScene;
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class RHIShaderProgramCache;
    class SceneRenderTargets;
    class ShaderMapCollection;
    struct BuiltinMeshPassPrograms;

    // GT-created one-shot owner transferred into a Draw command. All render-side
    // mutation and destruction happens on the logical Rendering Thread.
    class SceneRenderer
    {
      public:
        explicit SceneRenderer(SceneViewFamily view_family);
        virtual ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;
        SceneRenderer(SceneRenderer&&) = delete;
        SceneRenderer& operator=(SceneRenderer&&) = delete;

        Extent output_extent() const
        {
            return view_family_.output_extent();
        }

        // Read-only inspection of prepared views on the logical Rendering Thread.
        const std::vector<ViewInfo>& view_infos() const
        {
            return view_infos_;
        }

        // Renderer frame orchestration calls this on the logical Rendering
        // Thread after it has begun the shared graphics recording.
        virtual RHIStatus render_scene_passes(RenderScene& render_scene, RHIDevice& device,
                                              RHIShaderProgramCache& shader_program_cache,
                                              RHIGraphicsCommandContext& context,
                                              SceneRenderTargets& scene_render_targets,
                                              const BuiltinMeshPassPrograms& mesh_pass_programs) = 0;
        virtual RHIStatus render_hit_proxy(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                           const ShaderMapCollection& shader_map, RHIGraphicsCommandContext& context,
                                           const RHITextureViewRef& id_view, const RHITextureViewRef& depth_view,
                                           HitProxyTable& table) = 0;

      protected:
        const SceneViewFamily& view_family() const
        {
            return view_family_;
        }
        std::vector<ViewInfo>& view_infos()
        {
            return view_infos_;
        }

      private:
        SceneViewFamily view_family_;
        std::vector<ViewInfo> view_infos_;
    };
} // namespace toy3d
