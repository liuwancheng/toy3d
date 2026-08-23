#pragma once

namespace toy3d
{
    class StaticMeshRenderData;
    class StaticMeshSceneProxy;

    // Frame-local Render-side draw input. ViewInfo owns this value for one Draw;
    // the referenced Proxy and RenderData remain owned by their existing domains.
    class MeshBatch final
    {
    public:
        MeshBatch(
            const StaticMeshSceneProxy& scene_proxy,
            const StaticMeshRenderData& render_data)
            : scene_proxy_(&scene_proxy), render_data_(&render_data)
        {}

        const StaticMeshSceneProxy& scene_proxy() const
        {
            return *scene_proxy_;
        }
        const StaticMeshRenderData& render_data() const
        {
            return *render_data_;
        }

    private:
        const StaticMeshSceneProxy* scene_proxy_ = nullptr;
        const StaticMeshRenderData* render_data_ = nullptr;
    };
}
