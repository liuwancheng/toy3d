#include "scene/placement/asset_placement.h"

#include "scene/editor_command_history.h"
#include "scene/placement/actor_factory.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    std::uint32_t place_static_mesh_asset(EditorWorkspace& workspace, World& world,
        ActorFactory& factory, EditorCommandHistory& history, const AssetPlacementRequest& request, std::string& error)
    {
        error.clear();
        const AssetLocation* asset = workspace.catalog().index.find(request.asset_id);
        if (!asset || asset->index.root_type != "toy3d.StaticMeshAssetData")
        { error = "The dragged StaticMesh asset is missing or has changed type. Refresh Content Browser."; return 0; }
        const auto geometry = read_static_mesh_asset(workspace.files(), asset->path);
        if (!geometry.succeeded()) { error = geometry.status().message; return 0; }
        PlacementRequest placed;
        placed.item = PlacementItemId::StaticMesh;
        placed.asset_id = request.asset_id;
        placed.static_mesh = create_static_mesh_from_asset(geometry.value(), factory.default_material());
        if (!placed.static_mesh) { error = "Could not create runtime StaticMesh geometry."; return 0; }
        placed.transform = request.transform;
        if (request.on_ground)
        {
            // Viewport creates identity rotation/scale. Preserve imported origin
            // while keeping the local mesh bottom on the Editor ground plane.
            placed.transform.translation.y -= placed.static_mesh->local_bounds().minimum.y;
        }
        const std::uint32_t actor_id = history.place_actor(world, placed);
        if (!actor_id) error = "Could not place StaticMesh Actor.";
        return actor_id;
    }
}
