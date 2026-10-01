#pragma once

#include "gamescene/world/world.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/view/scene_view.h"
#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    // A GT-owned preview World observes a separate Renderer-owned scene.
    // It never changes the level World, selection, camera, or command history.
    class ThumbnailPreviewScene final
    {
      public:
        bool initialize(SceneInterface& scene, MaterialInstanceRef material);
        bool prepare(StaticMeshAssetGeometry geometry);
        SceneView view() const;
        void clear_mesh();
        void shutdown();

      private:
        World world_;
        MaterialInstanceRef material_;
        std::uint32_t mesh_actor_id_ = 0;
    };
}
