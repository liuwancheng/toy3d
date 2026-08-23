#pragma once

#include <cstdint>

namespace toy3d
{
    class LocalVertexFactory;
    class MaterialRenderProxy;
    class StaticMeshRenderData;
    class StaticMeshSceneProxy;

    // Frame-local Render-side draw input. ViewInfo owns this value for one Draw;
    // Proxy, RenderData, VertexFactory, and Material remain owned by their
    // existing domains and are kept valid by RenderCommand FIFO ordering.
    class MeshBatch final
    {
    public:
        MeshBatch(
            const StaticMeshSceneProxy& scene_proxy,
            const StaticMeshRenderData& render_data,
            const LocalVertexFactory& vertex_factory,
            MaterialRenderProxy& material_render_proxy,
            std::uint32_t first_index,
            std::uint32_t index_count)
            : scene_proxy_(&scene_proxy),
              render_data_(&render_data),
              vertex_factory_(&vertex_factory),
              material_render_proxy_(&material_render_proxy),
              first_index_(first_index),
              index_count_(index_count)
        {}

        const StaticMeshSceneProxy& scene_proxy() const
        {
            return *scene_proxy_;
        }
        const StaticMeshRenderData& render_data() const
        {
            return *render_data_;
        }
        const LocalVertexFactory& vertex_factory() const
        {
            return *vertex_factory_;
        }
        MaterialRenderProxy& material_render_proxy() const
        {
            return *material_render_proxy_;
        }
        std::uint32_t first_index() const { return first_index_; }
        std::uint32_t index_count() const { return index_count_; }

    private:
        const StaticMeshSceneProxy* scene_proxy_ = nullptr;
        const StaticMeshRenderData* render_data_ = nullptr;
        const LocalVertexFactory* vertex_factory_ = nullptr;
        MaterialRenderProxy* material_render_proxy_ = nullptr;
        std::uint32_t first_index_ = 0;
        std::uint32_t index_count_ = 0;
    };
}
