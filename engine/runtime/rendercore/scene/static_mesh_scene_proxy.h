#pragma once

#include "rendercore/scene/primitive_scene_proxy.h"

#include <vector>

namespace toy3d
{
    class MaterialRenderProxy;
    class StaticMeshRenderData;

    // Static-mesh-specific Render-side state. Resource references are non-owning and
    // are protected by Proxy update/remove/release FIFO ordering.
    class StaticMeshSceneProxy final : public PrimitiveSceneProxy
    {
      public:
        StaticMeshSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                             StaticMeshRenderData* render_data,
                             std::vector<MaterialRenderProxy*> material_render_proxies, std::uint32_t actor_id = 0,
                             std::uint32_t component_id = 0, bool cast_shadows = true, bool receives_shadows = true);
        ~StaticMeshSceneProxy() override = default;

        RHIStatus begin_init_resources(RenderResourceManager& manager) override;
        RHIStatus release_resources(RenderResourceManager& manager, bool release_shared_geometry) override;
        bool shares_geometry_resources(const PrimitiveSceneProxy& other) const override;
        bool resources_drawable() const override;
        std::size_t mesh_section_count() const override;
        RHIStatus collect_mesh_batches(std::vector<MeshBatch>& batches) const override;

        StaticMeshRenderData* render_data() const
        {
            return render_data_;
        }

      private:
        StaticMeshRenderData* render_data_ = nullptr;
    };
} // namespace toy3d
