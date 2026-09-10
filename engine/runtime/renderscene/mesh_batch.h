#pragma once

#include "drivers/rhi/rhi_resource.h"
#include "shader_parameters/builtin_shader_parameters.generated.h"

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
        MeshBatch(const StaticMeshSceneProxy& scene_proxy, const StaticMeshRenderData& render_data,
                  const LocalVertexFactory& vertex_factory, MaterialRenderProxy& material_render_proxy,
                  std::uint32_t first_index, std::uint32_t index_count);

        const StaticMeshSceneProxy& scene_proxy() const { return *scene_proxy_; }
        const StaticMeshRenderData& render_data() const { return *render_data_; }
        const LocalVertexFactory& vertex_factory() const { return *vertex_factory_; }
        MaterialRenderProxy& material_render_proxy() const { return *material_render_proxy_; }
        const ObjectShaderParameters& object_shader_parameters() const { return object_shader_parameters_; }
        std::uint64_t object_data_generation() const { return object_data_generation_; }
        const RHIBindingSetRef& material_binding() const { return material_binding_; }
        const RHIBindingSetRef& object_binding() const { return object_binding_; }
        // MaterialRenderProxy remains the persistent owner; the renderer only
        // snapshots its current binding into this frame-local draw input.
        void publish_material_binding(RHIBindingSetRef binding_set);
        // create_object_shader_bindings() is the only frame-local creation path;
        // mesh passes consume the published draw-data reference directly.
        void publish_object_binding(RHIBindingSetRef binding_set);
        std::uint32_t first_index() const { return first_index_; }
        std::uint32_t index_count() const { return index_count_; }

      private:
        const StaticMeshSceneProxy* scene_proxy_ = nullptr;
        const StaticMeshRenderData* render_data_ = nullptr;
        const LocalVertexFactory* vertex_factory_ = nullptr;
        MaterialRenderProxy* material_render_proxy_ = nullptr;
        ObjectShaderParameters object_shader_parameters_;
        std::uint64_t object_data_generation_ = 0u;
        RHIBindingSetRef material_binding_;
        RHIBindingSetRef object_binding_;
        std::uint32_t first_index_ = 0;
        std::uint32_t index_count_ = 0;
    };
} // namespace toy3d
