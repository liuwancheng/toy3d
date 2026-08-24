#pragma once

#include "drivers/rhi/rhi_viewport_context.h"
#include "rendercore/view/scene_view.h"
#include "renderscene/view/view_info.h"

#include <vector>

namespace toy3d
{
    class RenderScene;
    class RenderResourceManager;
    class Renderer;
    class RHIDevice;

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

        // Render-side frame policy used by Renderer once its internal domain
        // owns the injected device, resource manager and primary viewport.
        virtual RHIResult<RHIFrameEndResult> render_frame(
            RenderScene& render_scene,
            RHIDevice& device,
            RenderResourceManager& resource_manager,
            RHIViewportContext& viewport,
            const RHITextureViewRef& depth_stencil_view) = 0;

    protected:
        const SceneViewFamily& view_family() const { return view_family_; }
        std::vector<ViewInfo>& view_infos() { return view_infos_; }

    private:
        friend class Renderer;

        virtual void render(RenderScene& render_scene) noexcept = 0;

        SceneViewFamily view_family_;
        std::vector<ViewInfo> view_infos_;
    };
}
