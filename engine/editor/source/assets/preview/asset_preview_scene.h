#pragma once

#include "gamescene/world/world.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/view/scene_view.h"
#include "asset/mesh/static_mesh_asset.h"
#include "rendercore/geometry/skeletal_mesh.h"
#include "rendercore/geometry/mesh_material_loader.h"

namespace toy3d
{
    class SkeletalMeshComponent;
    class AnimationSequence;
    struct MeshPreviewAsset;
    enum class MaterialPreviewMesh
    {
        Sphere,
        Plane,
        Cube
    };
    // Each window owns its settings; submitted frames copy immutable values.
    struct PreviewSceneSettings
    {
        PreviewSceneSettings();
        AssetId environment;
        float environment_intensity = 1.0f;
        float environment_rotation = 0.0f;
        float light_intensity = 2.0f;
        Vector3 light_color{1.0f, 1.0f, 1.0f};
        float light_yaw = -40.0f;
        float light_pitch = -45.0f;
        float exposure_ev = 0.0f;
        bool show_environment = true;
        bool show_floor = true;
        bool show_shadows = true;
    };
    bool operator==(const PreviewSceneSettings& left, const PreviewSceneSettings& right);
    bool validate_preview_scene_settings(const PreviewSceneSettings& settings);

    struct MaterialPreviewSettings
    {
        PreviewSceneSettings scene;
        MaterialPreviewMesh mesh = MaterialPreviewMesh::Sphere;
        float camera_yaw = 40.0f;
        float camera_pitch = 22.0f;
        float camera_distance = 600.0f;
        Extent extent{384u, 384u};
    };
    bool operator==(const MaterialPreviewSettings& left, const MaterialPreviewSettings& right);
    bool validate_material_preview_settings(const MaterialPreviewSettings& settings);

    // A GT-owned preview World observes a separate Renderer-owned scene.
    // It never changes the level World, selection, camera, or command history.
    class AssetPreviewScene final
    {
      public:
        void set_material_resolver(MeshMaterialResolver resolver)
        {
            material_resolver_ = std::move(resolver);
        }
        bool initialize(SceneInterface& scene, MaterialInstanceRef material, SceneEnvironmentSettings environment = {},
                        TextureRef cube = {});
        bool prepare(StaticMeshAssetGeometry geometry, MaterialInterfaceRef material = {});
        bool prepare(StaticMeshRef mesh);
        // Interactive mesh previews preserve authored centimeters; thumbnails normalize copies.
        bool prepare_static(const StaticMeshAssetGeometry& geometry);
        bool set_mesh_visible(bool visible);
        bool prepare_skeletal(SkeletalMeshRef mesh, std::shared_ptr<const AnimationSequence> sequence = {});
        bool prepare_skeletal(const MeshPreviewAsset& asset);
        SkeletalMeshComponent* skeletal_component();
        const Vector3& frame_center() const;
        float frame_radius() const;
        bool configure(const PreviewSceneSettings& settings, TextureRef cube);
        bool configure_thumbnail();
        SceneView view() const;
        SceneView view(const MaterialPreviewSettings& settings) const;
        void clear_mesh();
        void clear_geometry();
        void shutdown();

      private:
        World world_;
        MaterialInstanceRef material_;
        MeshMaterialResolver material_resolver_;
        MaterialInstanceRef floor_material_;
        StaticMeshRef floor_mesh_;
        StaticMeshDesc floor_geometry_;
        float floor_height_ = 0.0f;
        SceneEnvironmentSettings thumbnail_environment_;
        TextureRef thumbnail_cube_;
        std::uint32_t light_actor_id_ = 0;
        std::uint32_t floor_actor_id_ = 0;
        std::uint32_t mesh_actor_id_ = 0;
        bool skeletal_ = false;
        Vector3 frame_center_;
        float frame_radius_ = 100.0f;
    };
} // namespace toy3d
