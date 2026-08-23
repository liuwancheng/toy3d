#pragma once

#include "renderscene/view/scene_renderer.h"

namespace toy3d
{
    class ForwardSceneRenderer final : public SceneRenderer
    {
    public:
        explicit ForwardSceneRenderer(SceneViewFamily view_family);
        ~ForwardSceneRenderer() override = default;

    private:
        bool init_views();
        void compute_view_visibility(const RenderScene& render_scene);
        void collect_mesh_batches();
        void render(RenderScene& render_scene) noexcept override;
    };
}
