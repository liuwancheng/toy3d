#include "rendercore/scene/static_mesh_scene_proxy.h"

#include <utility>

namespace toy3d
{
    StaticMeshSceneProxy::StaticMeshSceneProxy(Matrix4 world_transform, AxisAlignedBounds world_bounds, bool visible,
                                               StaticMeshRenderData* render_data,
                                               std::vector<MaterialRenderProxy*> material_render_proxies,
                                               std::uint32_t actor_id, std::uint32_t component_id, bool cast_shadows,
                                               bool receives_shadows)
        : PrimitiveSceneProxy(std::move(world_transform), std::move(world_bounds), visible, actor_id, component_id,
                              cast_shadows, receives_shadows),
          render_data_(render_data), material_render_proxies_(std::move(material_render_proxies))
    {
    }

    void StaticMeshSceneProxy::set_material_render_proxies(std::vector<MaterialRenderProxy*> materials)
    {
        material_render_proxies_ = std::move(materials);
    }
} // namespace toy3d
