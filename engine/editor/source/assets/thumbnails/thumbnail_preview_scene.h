#pragma once

#include "gamescene/world/world.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/view/scene_view.h"
#include "asset/mesh/static_mesh_asset.h"

namespace toy3d
{
    // Editor session state, copied into each request; never persisted in the Material or level.
    struct MaterialPreviewSettings
    {
        MaterialPreviewSettings();
        AssetId environment;
        float environment_intensity = 1.0f;
        float environment_rotation = 0.0f;
        float light_intensity = 2.0f;
        Vector3 light_color{1.0f, 1.0f, 1.0f};
        float light_yaw = -40.0f;
        float light_pitch = -45.0f;
        float exposure_ev = 0.0f;
        float camera_yaw = 40.0f;
        float camera_pitch = 22.0f;
        float camera_distance = 430.0f;
        Extent extent{384u, 384u};
        bool show_environment = true;
        bool show_floor = true;
        bool show_shadows = true;
    };
    bool operator==(const MaterialPreviewSettings& left, const MaterialPreviewSettings& right);
    bool validate_material_preview_settings(const MaterialPreviewSettings& settings);

    // A GT-owned preview World observes a separate Renderer-owned scene.
    // It never changes the level World, selection, camera, or command history.
    class ThumbnailPreviewScene final
    {
      public:
        bool initialize(SceneInterface& scene, MaterialInstanceRef material, SceneEnvironmentSettings environment = {},
                        TextureRef cube = {});
        bool prepare(StaticMeshAssetGeometry geometry, MaterialInterfaceRef material = {});
        bool configure(const MaterialPreviewSettings& settings, TextureRef cube);
        bool configure_thumbnail();
        SceneView view() const;
        SceneView view(const MaterialPreviewSettings& settings) const;
        void clear_mesh();
        void shutdown();

      private:
        World world_;
        MaterialInstanceRef material_;
        MaterialInstanceRef floor_material_;
        StaticMeshRef floor_mesh_;
        float floor_height_ = 0.0f;
        SceneEnvironmentSettings thumbnail_environment_;
        TextureRef thumbnail_cube_;
        std::uint32_t light_actor_id_ = 0;
        std::uint32_t floor_actor_id_ = 0;
        std::uint32_t mesh_actor_id_ = 0;
    };
} // namespace toy3d
