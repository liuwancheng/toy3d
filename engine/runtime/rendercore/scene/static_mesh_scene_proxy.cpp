#include "rendercore/scene/static_mesh_scene_proxy.h"

#include <utility>

namespace toy3d
{
    StaticMeshSceneProxy::StaticMeshSceneProxy(
        Matrix4 world_transform,
        AxisAlignedBounds world_bounds,
        bool visible,
        StaticMeshRenderData* render_data,
        std::vector<MaterialRenderProxy*> material_render_proxies)
        : PrimitiveSceneProxy(
              std::move(world_transform),
              std::move(world_bounds),
              visible),
          render_data_(render_data),
          material_render_proxies_(std::move(material_render_proxies))
    {
    }
}
