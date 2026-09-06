#pragma once

#include "drivers/rhi/rhi_result.h"
#include "rendercore/view/scene_view.h"
#include "renderscene/view/view_info.h"

#include <vector>

namespace toy3d
{
    class RenderScene;
    class RHIDevice;
    class RHIGraphicsCommandContext;
    class SceneRenderTargets;

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

        UIntVector2 output_size() const
        {
            return view_family_.output_size();
        }

        // Renderer frame orchestration calls this on the logical Rendering
        // Thread after it has begun the shared graphics recording.
        virtual RHIStatus render_scene_passes(
            RenderScene& render_scene,
            RHIDevice& device,
            RHIGraphicsCommandContext& context,
            SceneRenderTargets& scene_render_targets) = 0;

    protected:
        const SceneViewFamily& view_family() const { return view_family_; }
        std::vector<ViewInfo>& view_infos() { return view_infos_; }

    private:
        SceneViewFamily view_family_;
        std::vector<ViewInfo> view_infos_;
    };
}
