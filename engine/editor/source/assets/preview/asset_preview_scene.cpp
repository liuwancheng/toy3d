#include "assets/preview/asset_preview_scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "asset/thumbnail/asset_thumbnail.h"
#include "assets/animation/animation_preview_asset.h"
#include "asset/texture/builtin_texture_assets.h"
#include "drivers/rhi/rhi_resource.h"
#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/actor/skeletal_mesh_actor.h"
#include "gamescene/scene_geometry.h"
#include "logging/logger.h"
#include "math/length_units.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "rendercore/frame_synchronization.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // PreviewSceneSettings: per-window environment, lighting and display policy
    // --------------------------------------------------------------------------
    PreviewSceneSettings::PreviewSceneSettings()
    {
        if (!AssetId::parse(builtin_courtyard_environment_id, environment))
        {
            TOY_LOG_ERROR("Invalid built-in preview courtyard identity.");
        }
    }

    bool operator==(const PreviewSceneSettings& a, const PreviewSceneSettings& b)
    {
        return a.environment == b.environment && a.environment_intensity == b.environment_intensity &&
               a.environment_rotation == b.environment_rotation && a.light_intensity == b.light_intensity &&
               a.light_color == b.light_color && a.light_yaw == b.light_yaw && a.light_pitch == b.light_pitch &&
               a.exposure_ev == b.exposure_ev && a.show_environment == b.show_environment &&
               a.show_floor == b.show_floor && a.show_shadows == b.show_shadows;
    }

    bool validate_preview_scene_settings(const PreviewSceneSettings& s)
    {
        return std::isfinite(s.environment_intensity) && s.environment_intensity >= 0 && s.environment_intensity <= 8 &&
               std::isfinite(s.environment_rotation) && std::abs(s.environment_rotation) <= 360 &&
               std::isfinite(s.light_intensity) && s.light_intensity >= 0 && s.light_intensity <= 16 &&
               is_finite(s.light_color) && s.light_color.x >= 0 && s.light_color.y >= 0 && s.light_color.z >= 0 &&
               s.light_color.x <= 1 && s.light_color.y <= 1 && s.light_color.z <= 1 && std::isfinite(s.light_yaw) &&
               std::abs(s.light_yaw) <= 360 && std::isfinite(s.light_pitch) && s.light_pitch >= -89 &&
               s.light_pitch <= -5 && std::isfinite(s.exposure_ev) && std::abs(s.exposure_ev) <= 8;
    }

    bool operator==(const MaterialPreviewSettings& a, const MaterialPreviewSettings& b)
    {
        return a.scene == b.scene && a.mesh == b.mesh && a.camera_yaw == b.camera_yaw &&
               a.camera_pitch == b.camera_pitch && a.camera_distance == b.camera_distance && a.extent == b.extent;
    }

    bool validate_material_preview_settings(const MaterialPreviewSettings& s)
    {
        return validate_preview_scene_settings(s.scene) &&
               (s.mesh == MaterialPreviewMesh::Sphere || s.mesh == MaterialPreviewMesh::Plane ||
                s.mesh == MaterialPreviewMesh::Cube) &&
               std::isfinite(s.camera_yaw) && std::abs(s.camera_yaw) <= 360 && std::isfinite(s.camera_pitch) &&
               s.camera_pitch >= -80 && s.camera_pitch <= 80 && std::isfinite(s.camera_distance) &&
               s.camera_distance >= 220 && s.camera_distance <= 1000 && s.extent.width >= 96 &&
               s.extent.width <= rhi_max_texture_readback_dimension && s.extent.height >= 96 &&
               s.extent.height <= rhi_max_texture_readback_dimension;
    }

    namespace
    {
        // Normalize only the preview copy; these lengths do not alter asset units.
        constexpr double k_preview_radius_cm = meters_to_centimeters(1.0f);
        constexpr float k_preview_near_clip_cm = meters_to_centimeters(0.05f);
        constexpr float k_preview_far_clip_cm = meters_to_centimeters(20.0f);
    } // namespace

    // --------------------------------------------------------------------------
    // AssetPreviewScene: private World implementation for asset previews and thumbnails
    // --------------------------------------------------------------------------
    bool AssetPreviewScene::initialize(SceneInterface& scene, MaterialInstanceRef material,
                                       SceneEnvironmentSettings environment, TextureRef cube)
    {
        thumbnail_environment_ = environment;
        thumbnail_cube_ = cube;
        if (!material || !world_.set_environment(std::move(environment), std::move(cube)) || !world_.bind_scene(scene))
        {
            return false;
        }
        material_ = std::move(material);
        floor_material_ = MaterialInstance::create(material_);
        if (!floor_material_ || !floor_material_->set_vector("base_color", vec4(0.35f, 0.35f, 0.35f, 1.0f)))
        {
            return false;
        }
        auto& light = world_.spawn_actor<DirectionalLightActor>();
        light_actor_id_ = light.actor_id();
        Transform transform;
        if (!try_make_rotation_from_forward_up(Vector3(-0.4f, -0.6f, 0.7f), Vector3(0, 1, 0), transform.rotation) ||
            !light.root_component()->set_local_transform(transform) || !light.light_component().set_intensity(2.0f))
        {
            return false;
        }
        if (!light.light_component().set_shadow_cascade_count(1) ||
            !light.light_component().set_shadow_map_resolution(1024) ||
            !light.light_component().set_shadow_distance(1500.0f))
        {
            return false;
        }
        StaticMeshDesc floor;
        floor.vertices = {{{-600, 0, -600}, {0, 1, 0}, {0, 0}},
                          {{600, 0, -600}, {0, 1, 0}, {1, 0}},
                          {{600, 0, 600}, {0, 1, 0}, {1, 1}},
                          {{-600, 0, 600}, {0, 1, 0}, {0, 1}}};
        for (auto& vertex : floor.vertices)
        {
            vertex.tangent = vec4(1, 0, 0, -1);
        }
        floor.valid_tangent_frame = true;
        // C++17 variant chooses the bounded UInt16 index format for this analytic plane.
        floor.indices = std::vector<std::uint16_t>{0, 2, 1, 0, 3, 2};
        floor.sections.push_back({0u, 6u, 0u});
        floor.material_slots.push_back(floor_material_);
        floor_geometry_ = std::move(floor);
        // Register the floor only when a live preview needs it. A hidden, never-drawn
        // component would block startup Shader validation on an upload that has no frame.
        world_.initialize();
        return true;
    }

    bool AssetPreviewScene::prepare(StaticMeshAssetGeometry geometry, MaterialInterfaceRef material)
    {
        clear_mesh();
        if (geometry.vertices.empty())
        {
            return false;
        }
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
            radius = std::max(radius, std::sqrt(x * x + y * y + z * z));
        }
        if (!(radius > 0.0) || !std::isfinite(radius))
        {
            return false;
        }
        for (auto& vertex : geometry.vertices)
        {
            vertex.position =
                Vector3(static_cast<float>((vertex.position.x - center_x) * k_preview_radius_cm / radius),
                        static_cast<float>((vertex.position.y - center_y) * k_preview_radius_cm / radius),
                        static_cast<float>((vertex.position.z - center_z) * k_preview_radius_cm / radius));
        }
        auto mesh = create_static_mesh_from_asset(geometry, material ? std::move(material) : material_);
        if (!mesh)
        {
            return false;
        }
        auto& actor = world_.spawn_actor<StaticMeshActor>();
        actor.static_mesh_component().set_static_mesh(std::move(mesh));
        mesh_actor_id_ = actor.actor_id();
        floor_height_ = static_cast<float>((minimum.y - center_y) * k_preview_radius_cm / radius) - 0.25f;
        return true;
    }

    bool AssetPreviewScene::prepare(StaticMeshRef mesh)
    {
        if (!mesh)
        {
            return false;
        }
        const auto& bounds = mesh->local_bounds();
        const Vector3 minimum(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
        const Vector3 maximum(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
        const float radius = length(maximum - minimum) * 0.5f;
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            return false;
        }
        clear_mesh();
        auto& actor = world_.spawn_actor<StaticMeshActor>();
        actor.static_mesh_component().set_static_mesh(clone_scene_geometry(mesh));
        mesh_actor_id_ = actor.actor_id();
        Transform transform;
        transform.scale = Vector3(k_preview_radius_cm / radius);
        transform.translation = -(minimum + maximum) * 0.5f * (k_preview_radius_cm / radius);
        floor_height_ =
            (bounds.minimum.y - (bounds.minimum.y + bounds.maximum.y) * 0.5f) * (k_preview_radius_cm / radius) - 0.25f;
        return actor.root_component()->set_local_transform(transform);
    }

    bool AssetPreviewScene::prepare_skeletal(SkeletalMeshRef mesh, std::shared_ptr<const AnimationSequence> sequence)
    {
        if (!mesh || mesh->asset().geometry.mesh.vertices.empty())
        {
            return false;
        }
        if (auto* component = skeletal_component())
        {
            if (component->skeletal_mesh() == mesh)
            {
                const auto status = component->set_animation(std::move(sequence));
                if (!status.succeeded())
                {
                    TOY_LOG_ERROR("Skeletal preview animation replacement failed: {}", status.message);
                    return false;
                }
                return !component->playback_state() || component->set_playing(false).succeeded();
            }
        }
        clear_mesh();
        auto& actor = world_.spawn_actor<SkeletalMeshActor>();
        mesh_actor_id_ = actor.actor_id();
        skeletal_ = true;
        const auto status = actor.skeletal_mesh_component().set_assets(mesh, std::move(sequence));
        if (!status.succeeded())
        {
            TOY_LOG_ERROR("Skeletal preview asset setup failed: {}", status.message);
            clear_mesh();
            return false;
        }
        if (actor.skeletal_mesh_component().playback_state())
        {
            const auto paused = actor.skeletal_mesh_component().set_playing(false);
            if (!paused.succeeded())
            {
                TOY_LOG_ERROR("Could not pause skeletal preview: {}", paused.message);
                clear_mesh();
                return false;
            }
        }
        Vector3 minimum = mesh->asset().geometry.mesh.vertices.front().position;
        Vector3 maximum = minimum;
        for (const auto& vertex : mesh->asset().geometry.mesh.vertices)
        {
            minimum.x = std::min(minimum.x, vertex.position.x);
            minimum.y = std::min(minimum.y, vertex.position.y);
            minimum.z = std::min(minimum.z, vertex.position.z);
            maximum.x = std::max(maximum.x, vertex.position.x);
            maximum.y = std::max(maximum.y, vertex.position.y);
            maximum.z = std::max(maximum.z, vertex.position.z);
        }
        frame_center_ = (minimum + maximum) * 0.5f;
        frame_radius_ = std::max(1.0f, length(maximum - minimum) * 0.5f);
        floor_height_ = minimum.y - 0.25f;
        return true;
    }

    bool AssetPreviewScene::prepare_skeletal(const AnimationPreviewAsset& asset)
    {
        if (!asset.mesh)
        {
            return false;
        }
        const auto mesh =
            SkeletalMesh::create(asset.layout, *asset.mesh,
                                 std::vector<MaterialInterfaceRef>(asset.mesh->data.material_slots.size(), material_));
        if (!mesh.succeeded() || !prepare_skeletal(mesh.value(), asset.sequence))
        {
            return false;
        }
        auto* component = skeletal_component();
        if (asset.sequence && (!component->seek(0.0).succeeded() || !component->set_playing(false).succeeded()))
        {
            return false;
        }
        const auto& deformation = component->deformation();
        if (deformation && deformation->has_mesh_bounds)
        {
            frame_center_ = (deformation->bounds_minimum + deformation->bounds_maximum) * 0.5f;
            frame_radius_ = std::max(1.0f, length(deformation->bounds_maximum - deformation->bounds_minimum) * 0.5f);
            floor_height_ = deformation->bounds_minimum.y - 0.25f;
        }
        return true;
    }

    SkeletalMeshComponent* AssetPreviewScene::skeletal_component()
    {
        auto* actor = skeletal_ ? world_.find_actor_by_id(mesh_actor_id_) : nullptr;
        return actor ? &static_cast<SkeletalMeshActor*>(actor)->skeletal_mesh_component() : nullptr;
    }

    const Vector3& AssetPreviewScene::frame_center() const
    {
        return frame_center_;
    }

    float AssetPreviewScene::frame_radius() const
    {
        return frame_radius_;
    }

    bool AssetPreviewScene::configure(const PreviewSceneSettings& settings, TextureRef cube)
    {
        if (!validate_preview_scene_settings(settings) || (settings.environment.valid() && !cube))
        {
            return false;
        }
        SceneEnvironmentSettings environment;
        if (settings.environment.valid())
        {
            environment.environment.asset_id = settings.environment;
            environment.environment.expected_type = "toy3d.EnvironmentAssetData";
        }
        environment.intensity = settings.environment_intensity;
        if (!try_make_quaternion_from_axis_angle(Vector3(0, 1, 0), Radians(settings.environment_rotation * k_pi / 180),
                                                 environment.rotation) ||
            !world_.set_environment(environment, std::move(cube)))
        {
            return false;
        }
        auto* light = static_cast<DirectionalLightActor*>(world_.find_actor_by_id(light_actor_id_));
        auto* floor = static_cast<StaticMeshActor*>(world_.find_actor_by_id(floor_actor_id_));
        if (!light)
        {
            return false;
        }
        const float yaw = settings.light_yaw * k_pi / 180;
        const float pitch = settings.light_pitch * k_pi / 180;
        const Vector3 forward(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
        Transform transform;
        if (!try_make_rotation_from_forward_up(forward, Vector3(0, 1, 0), transform.rotation) ||
            !light->root_component()->set_local_transform(transform) ||
            !light->light_component().set_intensity(settings.light_intensity) ||
            !light->light_component().set_color(settings.light_color))
        {
            return false;
        }
        light->light_component().set_cast_shadows(settings.show_shadows);
        if (!floor && settings.show_floor)
        {
            // Removing the last proxy ends its render-data lifetime. Reopening
            // creates fresh render data from the small owned CPU floor description.
            floor_mesh_ = StaticMesh::create(floor_geometry_);
            if (!floor_mesh_)
            {
                return false;
            }
            floor = &world_.spawn_actor<StaticMeshActor>();
            floor_actor_id_ = floor->actor_id();
            floor->static_mesh_component().set_static_mesh(floor_mesh_);
            floor->static_mesh_component().set_cast_shadows(false);
        }
        if (floor)
        {
            Transform floor_transform;
            floor_transform.translation.y = floor_height_;
            floor_transform.translation.x = frame_center_.x;
            floor_transform.translation.z = frame_center_.z;
            if (!floor->root_component()->set_local_transform(floor_transform))
            {
                return false;
            }
            floor->static_mesh_component().set_visible(settings.show_floor);
        }
        return true;
    }

    bool AssetPreviewScene::configure_thumbnail()
    {
        PreviewSceneSettings settings;
        settings.environment = thumbnail_environment_.environment.asset_id;
        settings.environment_intensity = thumbnail_environment_.intensity;
        settings.show_floor = false;
        settings.show_shadows = false;
        if (!configure(settings, thumbnail_cube_))
        {
            return false;
        }
        auto* light = world_.find_actor_by_id(light_actor_id_);
        Transform transform;
        return try_make_rotation_from_forward_up(Vector3(-0.4f, -0.6f, 0.7f), Vector3(0, 1, 0), transform.rotation) &&
               light && light->root_component()->set_local_transform(transform);
    }

    SceneView AssetPreviewScene::view(const MaterialPreviewSettings& settings) const
    {
        const float yaw = settings.camera_yaw * k_pi / 180;
        const float pitch = settings.camera_pitch * k_pi / 180;
        const Vector3 position =
            Vector3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)) *
            settings.camera_distance;
        Vector3 direction;
        Quaternion rotation;
        if (!try_normalize(-position, direction) ||
            !try_make_rotation_from_forward_up(direction, Vector3(0, 1, 0), rotation))
        {
            TOY_LOG_ERROR("Material preview camera orientation is invalid.");
        }
        return SceneView(position, rotation, direction, {0, 0, settings.extent.width, settings.extent.height},
                         settings.extent, CameraProjectionMode::Perspective, Radians(0.785398163f),
                         k_preview_near_clip_cm, k_preview_far_clip_cm);
    }

    SceneView AssetPreviewScene::view() const
    {
        const Vector3 center = skeletal_ ? frame_center_ : Vector3();
        const Vector3 position = center + Vector3(2.5f, 1.7f, -3.0f) * (skeletal_ ? frame_radius_ : 100.0f);
        Vector3 direction;
        Quaternion rotation;
        if (!try_normalize(center - position, direction) ||
            !try_make_rotation_from_forward_up(direction, Vector3(0, 1, 0), rotation))
        {
            TOY_LOG_ERROR("Thumbnail camera orientation is invalid.");
        }
        const Extent extent{thumbnail_default_size, thumbnail_default_size};
        return SceneView(position, rotation, direction, IntRect{0, 0, extent.width, extent.height}, extent,
                         CameraProjectionMode::Perspective, Radians(0.785398163f), k_preview_near_clip_cm,
                         skeletal_ ? std::max(k_preview_far_clip_cm, frame_radius_ * 20.0f) : k_preview_far_clip_cm);
    }

    void AssetPreviewScene::clear_mesh()
    {
        if (auto* actor = world_.find_actor_by_id(mesh_actor_id_))
        {
            if (!world_.destroy_actor(*actor))
            {
                TOY_LOG_ERROR("Thumbnail mesh teardown failed.");
            }
        }
        mesh_actor_id_ = 0;
        skeletal_ = false;
        frame_center_ = {};
        frame_radius_ = 100.0f;
    }

    void AssetPreviewScene::clear_geometry()
    {
        clear_mesh();
        if (auto* floor = world_.find_actor_by_id(floor_actor_id_))
        {
            if (!world_.destroy_actor(*floor))
            {
                TOY_LOG_ERROR("Preview floor teardown failed.");
            }
        }
        floor_actor_id_ = 0;
        floor_mesh_.reset();
    }

    void AssetPreviewScene::shutdown()
    {
        clear_geometry();
        for (const auto id : world_.actor_ids())
        {
            if (auto* actor = world_.find_actor_by_id(id))
            {
                if (!world_.destroy_actor(*actor))
                {
                    TOY_LOG_ERROR("Thumbnail light teardown failed.");
                }
            }
        }
        if (world_.scene_interface() && !world_.unbind_scene())
        {
            TOY_LOG_ERROR("Thumbnail scene unbind failed.");
        }
        if (floor_material_ && !flush_rendering_commands().succeeded())
        {
            TOY_LOG_ERROR("Could not drain preview floor references during shutdown.");
        }
        material_.reset();
        // Unregister all components before dropping their independent material owners.
        floor_mesh_.reset();
        floor_geometry_ = {};
        MaterialInstance::release(floor_material_);
        thumbnail_cube_.reset();
        light_actor_id_ = 0;
        floor_actor_id_ = 0;
    }
} // namespace toy3d
