#pragma once

#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/render_resource.h"
#include "rendercore/geometry/static_mesh_render_data.h"

#include <vector>

namespace toy3d
{
    class MaterialRenderProxy;
    class StaticMeshRenderData;

    // The proxy transports owned geometry to RT admission, then holds a rendering
    // reference independently of asset caches and other scenes.
    class StaticMeshSceneProxy final : public PrimitiveSceneProxy
    {
      public:
        StaticMeshSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                             StaticMeshRenderData* render_data,
                             std::vector<MaterialRenderProxy*> material_render_proxies, std::uint32_t actor_id = 0,
                             std::uint32_t component_id = 0, bool cast_shadows = true, bool receives_shadows = true);
        ~StaticMeshSceneProxy() override = default;

        RHIStatus begin_init_resources(RenderResourceManager& manager) override;
        bool resources_drawable() const override;
        std::size_t mesh_section_count() const override;
        RHIStatus collect_mesh_batches(std::vector<MeshBatch>& batches) const override;

        StaticMeshRenderData* render_data() const
        {
            return render_data_;
        }

      private:
        StaticMeshRenderData* render_data_ = nullptr;
        std::shared_ptr<StaticMeshRenderData> pending_owner_;
        RenderResourceRef<StaticMeshRenderData> geometry_;
    };
} // namespace toy3d
