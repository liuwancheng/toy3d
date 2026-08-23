#pragma once

#include "rendercore/view/scene_view.h"
#include "renderscene/view/view_info.h"

#include <vector>

namespace toy3d
{
    class RenderScene;
    class Renderer;

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
