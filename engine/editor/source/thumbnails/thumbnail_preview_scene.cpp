#include "thumbnails/thumbnail_preview_scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "asset_thumbnail/asset_thumbnail.h"
#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "logging/logger.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"

namespace toy3d
{
    bool ThumbnailPreviewScene::initialize(SceneInterface& scene, MaterialInstanceRef material)
    {
        if (!material || !world_.bind_scene(scene)) return false;
        material_ = std::move(material);
        auto& light = world_.spawn_actor<DirectionalLightActor>();
        Transform transform;
        if (!try_make_rotation_from_forward_up(Vector3(-0.4f, -0.6f, 0.7f), Vector3(0, 1, 0),
                                                transform.rotation) ||
            !light.root_component()->set_local_transform(transform) ||
            !light.light_component().set_intensity(2.0f)) return false;
        world_.initialize();
        return true;
    }

    bool ThumbnailPreviewScene::prepare(StaticMeshAssetGeometry geometry)
    {
        clear_mesh();
        if (geometry.vertices.empty()) return false;
        Vector3 minimum = geometry.vertices.front().position;
        Vector3 maximum = minimum;
        for (const auto& vertex : geometry.vertices)
        {
            minimum.x = std::min(minimum.x, vertex.position.x);
            minimum.y = std::min(minimum.y, vertex.position.y);
            minimum.z = std::min(minimum.z, vertex.position.z);
            maximum.x = std::max(maximum.x, vertex.position.x);
            maximum.y = std::max(maximum.y, vertex.position.y);
            maximum.z = std::max(maximum.z, vertex.position.z);
        }
        // Normalize a copy in double precision. Huge source coordinates cannot
        // overflow camera placement, and the actual asset geometry stays intact.
        const double center_x = (static_cast<double>(minimum.x) + maximum.x) * 0.5;
        const double center_y = (static_cast<double>(minimum.y) + maximum.y) * 0.5;
        const double center_z = (static_cast<double>(minimum.z) + maximum.z) * 0.5;
        double radius = 0.0;
        for (const auto& vertex : geometry.vertices)
        {
            const double x = vertex.position.x - center_x;
            const double y = vertex.position.y - center_y;
            const double z = vertex.position.z - center_z;
            radius = std::max(radius, std::sqrt(x*x + y*y + z*z));
        }
        if (!(radius > 0.0) || !std::isfinite(radius)) return false;
        for (auto& vertex : geometry.vertices)
        {
            vertex.position = Vector3(static_cast<float>((vertex.position.x - center_x) * 100.0 / radius),
                                      static_cast<float>((vertex.position.y - center_y) * 100.0 / radius),
                                      static_cast<float>((vertex.position.z - center_z) * 100.0 / radius));
        }
        auto mesh = create_static_mesh_from_asset(geometry, material_);
        if (!mesh) return false;
        auto& actor = world_.spawn_actor<StaticMeshActor>();
        actor.static_mesh_component().set_static_mesh(std::move(mesh));
        mesh_actor_id_ = actor.actor_id();
        return true;
    }

    SceneView ThumbnailPreviewScene::view() const
    {
        const Vector3 position(250.0f, 170.0f, -300.0f);
        Vector3 direction;
        Quaternion rotation;
        if (!try_normalize(-position, direction) ||
            !try_make_rotation_from_forward_up(direction, Vector3(0, 1, 0), rotation))
            TOY_LOG_ERROR("Thumbnail camera orientation is invalid.");
        const Extent extent{thumbnail_default_size, thumbnail_default_size};
        return SceneView(position, rotation, direction,
                         IntRect{0, 0, extent.width, extent.height}, extent,
                         CameraProjectionMode::Perspective, Radians(0.785398163f), 5.0f, 2000.0f);
    }

    void ThumbnailPreviewScene::clear_mesh()
    {
        if (auto* actor = world_.find_actor_by_id(mesh_actor_id_))
            if (!world_.destroy_actor(*actor)) TOY_LOG_ERROR("Thumbnail mesh teardown failed.");
        mesh_actor_id_ = 0;
    }

    void ThumbnailPreviewScene::shutdown()
    {
        clear_mesh();
        for (const auto id : world_.actor_ids())
            if (auto* actor = world_.find_actor_by_id(id))
                if (!world_.destroy_actor(*actor)) TOY_LOG_ERROR("Thumbnail light teardown failed.");
        if (world_.scene_interface() && !world_.unbind_scene()) TOY_LOG_ERROR("Thumbnail scene unbind failed.");
        material_.reset();
    }
}
