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
                             std::vector<MaterialRenderProxy*> material_render_proxies);
        ~StaticMeshSceneProxy() override = default;

        StaticMeshRenderData* render_data() const { return render_data_; }
        const std::vector<MaterialRenderProxy*>& material_render_proxies() const { return material_render_proxies_; }

      private:
        StaticMeshRenderData* render_data_ = nullptr;
        std::vector<MaterialRenderProxy*> material_render_proxies_;
    };
} // namespace toy3d
