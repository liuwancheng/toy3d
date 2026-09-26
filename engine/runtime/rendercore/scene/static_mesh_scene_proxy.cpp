#include "rendercore/scene/static_mesh_scene_proxy.h"

#include <utility>

namespace toy3d
{
    StaticMeshSceneProxy::StaticMeshSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                               StaticMeshRenderData* render_data,
                                               std::vector<MaterialRenderProxy*> material_render_proxies,
                                               std::uint32_t actor_id, std::uint32_t component_id)
        : PrimitiveSceneProxy(std::move(world_transform), std::move(world_bounds), visible, actor_id,
                              component_id), render_data_(render_data),
          material_render_proxies_(std::move(material_render_proxies))
    {
    }
} // namespace toy3d
