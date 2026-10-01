#include "commands/editor_command_history.h"
#include "placement/actor_placement.h"
#include "viewport/actor_icons.h"
#include "viewport/scene_viewport.h"
#include "selection/editor_selection.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <limits>
#include <vector>

#include "imgui.h"

#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/camera_actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "rendercore/geometry/static_mesh_asset_loader.h"
#include "gamescene/world/world.h"
#include "math/matrix_construction.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene_interface.h"

namespace
{
    int failures = 0;
    void check(bool result, const char* message)
    {
        if (!result) { ++failures; std::cerr << message << '\n'; }
    }

    // An immediate CPU scene verifies the component bridge without GPU or render-thread state.
    class TestScene final : public toy3d::SceneInterface
    {
      public:
        void add_primitive(std::unique_ptr<toy3d::PrimitiveSceneProxy>) override {}
        void update_primitive_transform(toy3d::PrimitiveSceneProxy*, toy3d::Matrix4,
                                        toy3d::AxisAlignedBounds, bool, bool, bool) override {}
        void remove_primitive(toy3d::PrimitiveSceneProxy*) override {}
        void update_primitive_materials(toy3d::PrimitiveSceneProxy*, std::vector<toy3d::MaterialRenderProxy*>) override {}
        void add_light(std::unique_ptr<toy3d::LightSceneProxy> proxy) override { lights.push_back(std::move(proxy)); }
        void update_light(toy3d::LightSceneProxy* proxy, toy3d::LightSceneData data) override { proxy->data = data; ++updates; }
        void remove_light(toy3d::LightSceneProxy* proxy) override
        {
            for (auto it = lights.begin(); it != lights.end(); ++it)
                if (it->get() == proxy) { lights.erase(it); return; }
            check(false, "Remove must reference an owned light");
        }
        std::vector<std::unique_ptr<toy3d::LightSceneProxy>> lights;
        int updates = 0;
    };
}

int main()
{
    using namespace toy3d;
    {
        World world;
        world.initialize();
        ActorFactory factory;
        EditorCommandHistory history(factory);
        PlacementRequest request;
        const auto original = history.place_actor(world, request);
        check(original != 0 && !world.find_actor_by_id(original)->has_begun_play(), "Editor placement must not begin play");
        Actor* actor = world.find_actor_by_id(original);
        history.begin(world, original, actor->root_component()->local_transform(), EditorTransformSource::Details);
        Transform moved;
        moved.translation = Vector3(2, 3, 4);
        check(actor->root_component()->set_local_transform(moved), "Move should apply");
        history.finish(world, EditorTransformSource::Details);
        check(history.undo(world) && history.undo(world) && world.actor_count() == 0, "Move then creation must undo in one timeline");
        check(history.redo(world) && world.actor_count() == 1, "Creation redo must reconstruct an Actor");
        const auto restored = world.actor_ids().front();
        check(restored != original && history.redo(world), "Transform redo must follow the reconstructed Actor ID");
        actor = world.find_actor_by_id(restored);
        check(actor->root_component()->local_transform().translation == moved.translation, "Remapped Transform must target the new Actor");
        check(history.delete_actor(world, restored) && world.actor_count() == 0, "Delete must be undoable");
        check(history.undo(world), "Undo deletion must restore the Actor");
        const auto deleted_restore = world.actor_ids().front();
        check(deleted_restore != restored && history.undo(world), "Earlier Transform undo must follow deletion reconstruction");
        const auto count = world.actor_count();
        request.item = static_cast<PlacementItemId>(999);
        check(history.place_actor(world, request) == 0 && world.actor_count() == count, "Unknown drag identity must not mutate World");
        check(history.redo(world), "Failed placement must preserve redo history");
        request.item = PlacementItemId::EmptyActor;
        request.transform.scale.x = 0;
        check(history.place_actor(world, request) == 0 && world.actor_count() == count, "Invalid transform must not leave a candidate Actor");
    }
    {
        World world;
        world.initialize();
        ActorFactory factory;
        EditorCommandHistory history(factory);
        PlacementRequest request;
        request.item = PlacementItemId::PointLight;
        auto id = history.place_actor(world, request);
        auto* actor = dynamic_cast<PointLightActor*>(world.find_actor_by_id(id));
        check(actor != nullptr, "Point light placement must compose a light root");
        history.begin(world, id, actor->root_component()->local_transform(), EditorTransformSource::Details);
        check(actor->light_component().set_intensity(5) && actor->light_component().set_range(1200), "Light property gesture");
        history.finish(world, EditorTransformSource::Details);
        check(history.undo(world) && actor->light_component().intensity() == 1 && actor->light_component().range() == 1000,
              "Undo must restore all light properties in one gesture");
        check(history.redo(world) && actor->light_component().intensity() == 5, "Redo must restore edited light values");
        check(history.delete_actor(world, id) && history.undo(world), "Light deletion must reconstruct a typed Actor");
        actor = dynamic_cast<PointLightActor*>(world.find_actor_by_id(world.actor_ids().front()));
        check(actor && actor->light_component().range() == 1200 && actor->light_component().intensity() == 5,
              "Deletion undo must preserve light values");
        check(history.undo(world) && actor->light_component().intensity() == 1,
              "Property undo must follow the reconstructed light ID");
    }
    {
        World world;
        world.initialize();
        ActorFactory factory;
        EditorCommandHistory history(factory);
        PlacementRequest request;
        request.item = PlacementItemId::Camera;
        request.transform.translation = Vector3(1, 2, -4);
        const auto id = history.place_actor(world, request);
        auto* actor = dynamic_cast<CameraActor*>(world.find_actor_by_id(id));
        check(actor && actor->root_component() == &actor->camera_component(), "Camera placement must own a camera root");
        if (actor)
        {
            SceneViewport viewport;
            const Extent extent{800, 400};
            std::vector<SceneView> editor_views;
            viewport.build_scene_views(world, editor_views, extent);
            check(editor_views.size() == 1 && viewport.view_camera(world, id), "View a placed camera");
            history.begin(world, id, actor->root_component()->local_transform(), EditorTransformSource::Details);
            check(actor->camera_component().set_perspective(75, 0.2f, 500), "Camera property gesture");
            history.finish(world, EditorTransformSource::Details);
            check(history.undo(world) && actor->camera_component().vertical_fov_degrees() == 60,
                  "Camera parameter undo must restore the complete projection");
            check(history.redo(world) && actor->camera_component().near_clip() == 0.2f,
                  "Camera parameter redo must restore clipping planes");
            std::vector<SceneView> views;
            viewport.build_scene_views(world, views, extent);
            check(views.size() == 1 && views.front().camera_position() == request.transform.translation &&
                  views.front().vertical_fov() == to_radians(Degrees(75)), "Camera view must copy world pose and projection");
            // C++17 get selects the known camera payload in this fixture.
        EditorActorState invalid = capture_actor_state(*actor, factory.component_editors());
            invalid.components.front().data.transform.translation = Vector3(99);
            std::get<CameraSettings>(invalid.components.front().data.properties).near_clip = std::get<CameraSettings>(invalid.components.front().data.properties).far_clip;
            check(!apply_actor_state(*actor, invalid, factory.component_editors()) && actor->root_component()->local_transform().translation ==
                  request.transform.translation, "Invalid projection must not partially apply its Transform");
            const float bad_values[] = {0, 180, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::infinity(),
                                        std::numeric_limits<float>::quiet_NaN()};
            for (const float value : bad_values)
                check(!actor->camera_component().set_perspective(value, 0.1f, 100) &&
                      actor->camera_component().vertical_fov_degrees() == 75, "Invalid FOV must preserve camera settings");
            invalid = capture_actor_state(*actor, factory.component_editors());
            invalid.components.front().data.transform.translation = Vector3(99);
            std::get<CameraSettings>(invalid.components.front().data.properties).near_clip = 1e30f;
            std::get<CameraSettings>(invalid.components.front().data.properties).far_clip = 2e30f;
            check(!apply_actor_state(*actor, invalid, factory.component_editors()) && actor->root_component()->local_transform().translation ==
                  request.transform.translation && actor->camera_component().near_clip() == 0.2f,
                  "Unrepresentable clipping projection must not mutate Transform or camera state");
            check(!actor->camera_component().set_perspective(60, std::numeric_limits<float>::denorm_min(),
                                                            std::numeric_limits<float>::denorm_min() * 2) &&
                  actor->camera_component().near_clip() == 0.2f,
                  "Underflow to a singular projection must preserve camera settings");
            check(actor->camera_component().set_perspective(45, 0.3f, 300) &&
                  views.front().vertical_fov() == to_radians(Degrees(75)), "Published SceneView must not reread its Component");
            Transform scaled = actor->root_component()->local_transform();
            scaled.scale = Vector3(2, 3, 4);
            check(actor->root_component()->set_local_transform(scaled), "Nonuniform camera scale should remain an Actor property");
            views.clear();
            viewport.build_scene_views(world, views, extent);
            check(views.front().camera_direction() == Vector3(0, 0, 1) && views.front().vertical_fov() == to_radians(Degrees(45)),
                  "Camera projection and direction must ignore nonuniform scale");
            auto& parent = world.spawn_actor<Actor>();
            auto& root = parent.create_component<SceneComponent>();
            check(parent.set_root_component(&root), "Camera attachment parent root");
            Transform parent_transform;
            parent_transform.translation = Vector3(4, 0, 0);
            check(try_make_rotation_from_forward_up(Vector3(1, 0, 0), Vector3(0, 1, 0), parent_transform.rotation) &&
                  root.set_local_transform(parent_transform) &&
                  actor->camera_component().attach_to(&root, AttachmentRule::KeepRelative), "Camera attachment should use scene transforms");
            views.clear();
            viewport.build_scene_views(world, views, extent);
            check(is_nearly_equal(views.front().camera_direction(), Vector3(1, 0, 0)) &&
                  is_nearly_equal(views.front().camera_position(), transform_position(root.world_transform(), scaled.translation)),
                  "Camera view must use inherited world position and rotation");
            check(actor->camera_component().attach_to(nullptr, AttachmentRule::KeepRelative), "Detach camera before history deletion");
            World other_world;
            auto& other_camera = other_world.spawn_actor<CameraActor>();
            check(other_camera.actor_id() == id && viewport.viewed_camera_id(other_world) == 0,
                  "World-local Actor IDs must not leak camera viewing across Worlds");
            views.clear();
            viewport.build_scene_views(other_world, views, extent);
            check(views.front().camera_position() == editor_views.front().camera_position(), "Another World must use the editor observer");
            check(!viewport.view_camera(world, parent.actor_id()) && viewport.viewed_camera_id(world) == id,
                  "Invalid camera target must preserve the current view");
            check(history.delete_actor(world, id) && viewport.viewed_camera_id(world) == 0, "Deleting a viewed camera must invalidate its target");
            views.clear();
            viewport.build_scene_views(world, views, extent);
            check(views.front().camera_position() == editor_views.front().camera_position() &&
                  views.front().camera_orientation() == editor_views.front().camera_orientation(), "Deletion must restore the untouched editor pose");
            check(history.undo(world), "Camera deletion undo");
            CameraActor* restored = nullptr;
            for (const auto actor_id : world.actor_ids())
                if (auto* candidate = dynamic_cast<CameraActor*>(world.find_actor_by_id(actor_id))) restored = candidate;
            check(restored && restored->actor_id() != id && restored->camera_component().vertical_fov_degrees() == 45 &&
                  restored->camera_component().far_clip() == 300 && viewport.viewed_camera_id(world) == 0,
                  "Deletion undo must restore camera data without silently resuming viewing");
            if (restored)
            {
                check(history.undo(world) && restored->camera_component().vertical_fov_degrees() == 60,
                      "Earlier camera edits must follow reconstructed IDs");
                check(history.redo(world) && restored->camera_component().vertical_fov_degrees() == 75 &&
                      viewport.view_camera(world, restored->actor_id()), "Redo and explicit viewing after reconstruction");
                viewport.exit_camera_view();
                views.clear();
                viewport.build_scene_views(world, views, extent);
                check(views.front().camera_position() == editor_views.front().camera_position(), "Explicit exit must restore the editor view");
            }
            views.clear();
            viewport.build_scene_views(world, views, Extent{});
            check(views.empty(), "Collapsed viewport must not create a zero-sized View");
            const std::uint32_t focus_id = world.actor_ids().front();
            check(viewport.focus_actor(world, focus_id) && viewport.viewed_camera_id(world) == 0,
                  "Focusing an Actor must leave camera view");
            views.clear();
            viewport.build_scene_views(world, views, extent);
            check(views.size() == 1 && is_finite(views.front().camera_position()) &&
                  views.front().camera_position() != editor_views.front().camera_position(),
                  "Focus must move the editor observation camera");
        }
    }
    {
        TestScene scene;
        World world;
        auto& light = world.spawn_actor<PointLightActor>();
        world.initialize();
        check(world.bind_scene(scene) && scene.lights.size() == 1, "Preexisting lights must register on scene bind");
        Transform transform;
        transform.translation = Vector3(1, 2, 3);
        check(light.root_component()->set_local_transform(transform), "Light movement should apply");
        check(light.light_component().set_range(7) && light.light_component().set_intensity(3), "Light property edits should apply");
        check(scene.lights.front()->data.position == transform.translation && scene.lights.front()->data.range == 7 &&
              scene.lights.front()->data.intensity == 3 && scene.updates >= 3, "Light updates must publish owned current values");
        light.light_component().set_enabled(false);
        check(!scene.lights.front()->data.enabled, "Disabled lights must publish disabled state");
        check(world.unbind_scene() && scene.lights.empty(), "World unbind must remove every light");
        check(world.bind_scene(scene) && scene.lights.size() == 1, "Rebind must reconstruct the light mirror");
        check(world.destroy_actor(light) && scene.lights.empty(), "Actor deletion must remove its light mirror");
        check(world.unbind_scene(), "Empty World should unbind");
    }
    {
        TestScene scene;
        World world;
        auto& light = world.spawn_actor<DirectionalLightActor>();
        world.initialize();
        check(world.bind_scene(scene), "Receiver bias fixture must bind the scene");
        ActorFactory factory;
        EditorCommandHistory history(factory);
        history.begin(world, light.actor_id(), light.root_component()->local_transform(), EditorTransformSource::Details);
        // C++17 get selects the known directional light payload in this fixture.
        EditorActorState edited = capture_actor_state(light, factory.component_editors());
        check(std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.cascade_count == 1 && std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.distribution_exponent == 3.0f &&
              std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.map_resolution == 2048, "Directional shadow defaults must use one cascade");
        std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.cascade_count = 3;
        std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.distribution_exponent = 4.0f;
        std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.map_resolution = 1024;
        std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.receiver_bias = 0.4f;
        check(apply_actor_state(light, edited, factory.component_editors()) && scene.lights.front()->data.shadow_receiver_bias == 0.4f,
              "Receiver bias edits must publish to the render scene");
        check(scene.lights.front()->data.shadow_cascade_count == 3 &&
              scene.lights.front()->data.cascade_distribution_exponent == 4.0f &&
              scene.lights.front()->data.shadow_map_resolution == 1024, "All shadow settings must reach the render mirror");
        history.finish(world, EditorTransformSource::Details);
        check(history.undo(world) && light.light_component().shadow_cascade_count() == 1 &&
              light.light_component().cascade_distribution_exponent() == 3.0f &&
              light.light_component().shadow_map_resolution() == 2048 &&
              light.light_component().shadow_receiver_bias() == 0.9f &&
              history.redo(world) && light.light_component().shadow_receiver_bias() == 0.4f,
              "Receiver bias must survive Details undo and redo");
        check(light.light_component().shadow_cascade_count() == 3 &&
              light.light_component().cascade_distribution_exponent() == 4.0f &&
              light.light_component().shadow_map_resolution() == 1024, "Redo must restore all cascade and resolution settings");
        const int updates = scene.updates;
        check(!light.light_component().set_shadow_cascade_count(0) &&
              !light.light_component().set_shadow_cascade_count(4) &&
              !light.light_component().set_cascade_distribution_exponent(0.0f) &&
              !light.light_component().set_cascade_distribution_exponent(11.0f) &&
              !light.light_component().set_cascade_distribution_exponent((std::numeric_limits<float>::quiet_NaN)()) &&
              !light.light_component().set_shadow_map_resolution(256) &&
              !light.light_component().set_shadow_map_resolution(4096) &&
              !light.light_component().set_shadow_map_resolution(1000) &&
              !light.light_component().set_shadow_map_resolution(8192) && scene.updates == updates,
              "Invalid cascade and resolution edits must not publish partial updates");
        check(!light.light_component().set_shadow_receiver_bias(-0.1f) &&
              !light.light_component().set_shadow_receiver_bias(1.1f) &&
              !light.light_component().set_shadow_receiver_bias((std::numeric_limits<float>::quiet_NaN)()) &&
              light.light_component().shadow_receiver_bias() == 0.4f && scene.updates == updates,
              "Invalid receiver bias must preserve the current light and render state");
        edited = capture_actor_state(light, factory.component_editors());
        const Transform before = edited.components.front().data.transform;
        edited.components.front().data.transform.translation.x += 100.0f;
        std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.receiver_bias = 2.0f;
        check(!apply_actor_state(light, edited, factory.component_editors()) && light.root_component()->local_transform().translation == before.translation,
              "An invalid receiver bias must reject the entire edit before changing Transform");
        edited = capture_actor_state(light, factory.component_editors());
        edited.components.front().data.transform.translation.x += 100;
        std::get<SceneDirectionalLightData>(edited.components.front().data.properties).shadow.cascade_count = 4;
        check(!apply_actor_state(light, edited, factory.component_editors()) && light.root_component()->local_transform().translation == before.translation,
              "Invalid cascade count must reject the complete editor state atomically");
        check(world.unbind_scene(), "Receiver bias fixture must unbind the scene");
    }
    {
        ActorFactory factory;
        check(factory.initialize(), "Builtin mesh creation must use the compiled shader schema");
        World world;
        PlacementRequest request;
        request.item = PlacementItemId::Cube;
        Actor* cube_actor = factory.create(world, request);
        check(cube_actor != nullptr, "Cube geometry should be reusable");
        if (auto* cube = dynamic_cast<StaticMeshActor*>(cube_actor))
        {
            const auto bounds = cube->static_mesh_component().world_bounds();
            const Vector3 size(bounds.maximum.x - bounds.minimum.x, bounds.maximum.y - bounds.minimum.y,
                               bounds.maximum.z - bounds.minimum.z);
            check(is_nearly_equal(size, Vector3(150)),
                  "Builtin cube must measure 150 cm with unit Transform scale");
        }
        request.item = PlacementItemId::Plane;
        check(factory.create(world, request) != nullptr, "Plane geometry should be reusable");
        StaticMeshAssetGeometry imported;
        imported.material_slots = {"ImportedSlot"};
        imported.vertices = {{{0,0,0},{0,0,1},{0,0}}, {{1,0,0},{0,0,1},{1,0}}, {{0,1,0},{0,0,1},{0,1}}};
        imported.vertices[0].color = {32, 64, 128, 255};
        imported.indices = {0,1,2};
        imported.sections = {{0,3,0}};
        request = {};
        request.item = PlacementItemId::StaticMesh;
        check(AssetId::parse("0123456789abcdef0123456789abcdef", request.asset_id), "Imported placement identity");
        request.static_mesh = create_static_mesh_from_asset(imported, factory.default_material());
        check(request.static_mesh != nullptr, "Asset geometry must adapt to runtime without Assimp");
        EditorCommandHistory history(factory);
        const auto mesh_id = history.place_actor(world, request);
        auto* mesh_actor = dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(mesh_id));
        check(mesh_actor != nullptr, "Asset placement must create StaticMeshActor");
        if (mesh_actor)
        {
            const auto mesh = mesh_actor->static_mesh_component().static_mesh();
            check(mesh && mesh != request.static_mesh && mesh->vertex_colors()[0] == imported.vertices[0].color,
                  "Placement must retain colors and use fresh render-resource ownership");
            history.begin(world, mesh_id, mesh_actor->root_component()->local_transform(),
                          EditorTransformSource::Details);
            mesh_actor->static_mesh_component().set_receives_shadows(false);
            history.finish(world, EditorTransformSource::Details);
            check(history.undo(world) && mesh_actor->static_mesh_component().receives_shadows() &&
                  history.redo(world) && !mesh_actor->static_mesh_component().receives_shadows(),
                  "Receive Shadows must survive a Details undo and redo");
            check(history.delete_actor(world, mesh_id) && history.undo(world), "Imported mesh delete undo");
            auto restored_id = world.actor_ids().back();
            mesh_actor = dynamic_cast<StaticMeshActor*>(world.find_actor_by_id(restored_id));
            check(mesh_actor && mesh_actor->static_mesh_component().static_mesh() != mesh,
                  "Restored asset Actor must not reuse released render resources");
            PlacementRequest restored;
            check(mesh_actor && factory.describe(*mesh_actor, restored) && restored.asset_id == request.asset_id &&
                  restored.static_mesh == request.static_mesh, "History must preserve asset identity and CPU prototype");
            check(history.redo(world) && history.undo(world), "Imported deletion redo remains reconstructible");
        }
        history.clear();
        for (auto id : world.actor_ids()) check(world.destroy_actor(*world.find_actor_by_id(id)), "Geometry Actor cleanup");
        // The caller's CPU prototype also owns a default-material reference.
        // Drop it before the factory performs the material's final release.
        request = {};
        factory.release();
    }
    {
        Matrix4 view;
        Matrix4 projection;
        Quaternion rotation;
        const Vector3 camera(0, 500, -500);
        check(try_make_rotation_from_forward_up(Vector3(0, -1, 1), Vector3(0, 1, 0), rotation) &&
              try_make_view_matrix(camera, rotation, view), "Placement camera construction");
        PerspectiveProjectionDesc desc;
        desc.vertical_fov = to_radians(Degrees(60));
        desc.aspect = 1;
        desc.near_clip = 10.0f;
        desc.far_clip = 100000;
        check(try_make_perspective_projection(desc, projection), "Placement projection construction");
        Transform result;
        const PlacementItem& cube = *find_placement_item(PlacementItemId::Cube);
        check(calculate_placement_transform(view, projection, camera, Vector2(0.5f, 0.5f), cube, result) &&
              std::abs(result.translation.y - cube.ground_offset) < 0.01f && std::abs(result.translation.z) < 0.01f,
              "Center ray must hit Y=0 and keep cube above ground");
        const Transform before = result;
        check(!calculate_placement_transform(view, Matrix4::zero(), camera, Vector2(0.5f), cube, result) &&
              result.translation == before.translation, "Singular projection must preserve output");
        check(!calculate_placement_transform(view, projection, camera, Vector2(-1, 0), cube, result), "Drops outside image must be rejected");
        check(try_make_view_matrix(camera, Quaternion::identity(), view) &&
              calculate_placement_transform(view, projection, camera, Vector2(0.5f), cube, result) &&
              result.translation.z > camera.z, "Parallel ground ray must fall back in front of camera");
        desc.far_clip = 4000000;
        check(try_make_perspective_projection(desc, projection) &&
              calculate_placement_transform(view, projection, camera, Vector2(0.5f), cube, result) &&
              is_nearly_equal(result.translation, camera + Vector3(0, 0, 800)),
              "Placement must work at the editor's maximum centimeter zoom range");
    }
    {
        Matrix4 projection;
        PerspectiveProjectionDesc desc;
        desc.vertical_fov = to_radians(Degrees(60));
        desc.aspect = 2;
        desc.near_clip = 0.1f;
        desc.far_clip = 100;
        check(try_make_perspective_projection(desc, projection), "Icon projection construction");
        const Vector2 origin(100, 50);
        const Vector2 size(800, 400);
        Vector2 center;
        float depth = 0;
        check(project_actor_icon(projection, Vector3(0, 0, 2), origin, size, center, depth) &&
              center == Vector2(500, 250), "Icon must align with the viewport image, including its offset");
        const float near_depth = depth;
        check(project_actor_icon(projection, Vector3(0, 1, 5), origin, size, center, depth) &&
              center.y < 250 && depth < near_depth, "Icon projection must use top-left Y and reversed-Z");
        const Vector2 before = center;
        const float before_depth = depth;
        check(!project_actor_icon(projection, Vector3(0, 0, -2), origin, size, center, depth) &&
              center == before && depth == before_depth, "Behind-camera icons must be rejected without changing output");
        check(!project_actor_icon(projection, Vector3(0, 0, 0.01f), origin, size, center, depth), "Near-clipped icons must be rejected");
        check(!project_actor_icon(projection, Vector3(0, 0, 200), origin, size, center, depth), "Far-clipped icons must be rejected");
        Vector2 a;
        Vector2 b;
        check(project_actor_segment(projection, Vector3(0, 0, -1), Vector3(0.2f, 0, 2), origin, size, a, b) &&
              is_finite(a) && is_finite(b) && a.x >= origin.x && a.x <= origin.x + size.x,
              "Frustum lines crossing the near plane must remain inside the image");
        const Vector2 before_a = a;
        const Vector2 before_b = b;
        check(!project_actor_segment(projection, Vector3(0, 0, -1), Vector3(0, 0, -2), origin, size, a, b) &&
              a == before_a && b == before_b, "Fully clipped frustum lines must preserve outputs");
        check(!project_actor_icon(projection, Vector3(100, 0, 2), origin, size, center, depth), "Offscreen icons must be rejected");
        check(!project_actor_icon(projection, Vector3(0, 0, 2), origin, Vector2(), center, depth), "Empty images must be rejected");
        check(!project_actor_icon(projection, Vector3((std::numeric_limits<float>::quiet_NaN)(), 0, 2),
                                  origin, size, center, depth), "Nonfinite icon positions must be rejected");
        DirectionalLightArrow arrow;
        check(project_light_direction(projection, Vector3(0, 0, 5), Vector3(1, 0, 0), origin, size, arrow) &&
              arrow.end.x > arrow.start.x && std::abs(arrow.end.y - arrow.start.y) < 0.001f,
              "Light rays along world +X must point right in the viewport");
        check(project_light_direction(projection, Vector3(0, 0, 5), Vector3(0, -1, 0), origin, size, arrow) &&
              arrow.end.y > arrow.start.y, "Downward light rays must point down in the viewport");
        check(project_light_direction(projection, Vector3(0, 0, 1), Vector3(0, 0, -1), origin, size, arrow) &&
              arrow.start == arrow.end && arrow.depth_delta > 0,
              "Camera-facing rays crossing the near plane must retain the toward-camera cue");
        check(project_light_direction(projection, Vector3(0, 0, 5), Vector3(0, 0, 1), origin, size, arrow) &&
              arrow.start == arrow.end && arrow.depth_delta < 0,
              "Camera-aligned forward rays must use the away-camera cue");
        check(project_light_direction(projection, Vector3(0, 0, 1), Vector3(1, 0, -1), origin, size, arrow) &&
              arrow.end.x > arrow.start.x && arrow.end.x <= origin.x + size.x + 0.001f,
              "Near-plane or viewport clipping must not reverse the projected light direction");
        const DirectionalLightArrow before_arrow = arrow;
        check(!project_light_direction(projection, Vector3(0, 0, 5), Vector3(), origin, size, arrow) &&
              arrow.start == before_arrow.start && arrow.end == before_arrow.end && arrow.depth_delta == before_arrow.depth_delta,
              "A zero light direction must fail without changing the arrow");
    }
    {
        // Headless ImGui exercises actual overlay hit order without a native
        // window or GPU: a farther, later-created light must not steal the hit.
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1000, 600);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.AddMousePosEvent(500, 250);
        World world;
        auto& near_light = world.spawn_actor<PointLightActor>();
        auto& far_light = world.spawn_actor<DirectionalLightActor>();
        Transform transform;
        transform.translation = Vector3(0, 0, 200);
        check(near_light.root_component()->set_local_transform(transform), "Near icon transform");
        transform.translation.z = 500;
        check(far_light.root_component()->set_local_transform(transform), "Far icon transform");
        Matrix4 projection;
        PerspectiveProjectionDesc desc;
        desc.aspect = 2;
        check(try_make_perspective_projection(desc, projection), "Overlay hit projection");
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Icon hit test");
        auto& camera = world.spawn_actor<CameraActor>();
        Transform camera_transform;
        camera_transform.translation = Vector3(0, 0, 100);
        check(camera.root_component()->set_local_transform(camera_transform), "Camera icon transform");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), camera.actor_id(), true, 2.0f) ==
              camera.actor_id(), "Camera and light icons must share frontmost picking");
        camera_transform.translation = Vector3(0, 0, 900);
        check(camera.root_component()->set_local_transform(camera_transform) &&
              draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), camera.actor_id(), true, 2.0f) ==
              near_light.actor_id(), "A farther camera must not steal a nearer light hit");
        camera_transform.translation = Vector3(0, 0, -200);
        check(camera.root_component()->set_local_transform(camera_transform), "Clipped camera icon transform");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), 0, true, 2.0f) ==
              near_light.actor_id(), "Overlapping light icons must select the frontmost Actor");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), 0, false, 2.0f) == 0,
              "Drag/drop or blocked image input must suppress icon hits");
        near_light.light_component().set_enabled(false);
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), near_light.actor_id(), true, 2.0f) ==
              near_light.actor_id(), "Disabled lights must remain selectable for editing");
        transform.translation = Vector3(0, 0, -200);
        check(near_light.root_component()->set_local_transform(transform), "Move near icon behind camera");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), 0, true, 2.0f) ==
              far_light.actor_id(), "A clipped light must not intercept another icon");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), far_light.actor_id(), true, 2.0f) ==
              far_light.actor_id(), "Selected directional-light depth cues must preserve icon hits");
        transform.translation = Vector3(0, 0, 500);
        check(try_make_rotation_from_forward_up(Vector3(1, -1, 1), Vector3(0, 1, 0), transform.rotation) &&
              far_light.root_component()->set_local_transform(transform), "Rotate the directional-light indicator");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), far_light.actor_id(), true, 2.0f) ==
              far_light.actor_id(), "Selected directional-light arrows must preserve icon hits after rotation");
        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext(context);
    }
    {
        // Real ImGui payload delivery exercises viewport input priority. Hover
        // must never mutate the World; only release produces an owned request.
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.DisplaySize = ImVec2(1000, 600);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        World world;
        ActorFactory factory;
        EditorCommandHistory history(factory);
        EditorSelection selection;
        SceneViewport viewport;
        AssetId id;
        check(AssetId::parse("12345678901234567890123456789012", id), "Drag fixture identity");
        auto draw = [&]()
        {
            // ImGuizmo BeginFrame creates its overlay window and consumes
            // NextWindow settings; apply the viewport layout afterwards.
            viewport.begin_frame();
            ImGui::SetNextWindowPos(ImVec2(200, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            viewport.draw(world, selection, history);
        };
        io.AddMousePosEvent(500, 300);
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        for (int frame = 0; frame < 2; ++frame)
        {
            io.AddMouseButtonEvent(0, frame == 0);
            ImGui::NewFrame();
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
            {
                ImGui::SetDragDropPayload(ASSET_DRAG_PAYLOAD, &id, sizeof(id));
                ImGui::TextUnformatted("StaticMesh test payload");
                ImGui::EndDragDropSource();
            }
            draw();
            AssetPlacementRequest request;
            const bool delivered = viewport.take_asset_placement(request);
            check(delivered == (frame == 1), "Only drag release delivers an AssetPlacementRequest");
            if (delivered)
            {
                check(request.asset_id == id && is_finite(request.transform.translation), "Delivery owns identity and placement transform");
                check(!viewport.take_asset_placement(request), "Placement request is consumed exactly once");
            }
            HitProxyRequest hit;
            check(!viewport.take_hit_request(hit) && world.actor_count() == 0, "Dragging blocks picking and performs no direct scene mutation");
            ImGui::Render();
        }
        ImGui::DestroyContext(context);
    }
    {
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1000, 600);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        World world;
        ActorFactory factory;
        EditorCommandHistory history(factory);
        EditorSelection selection;
        SceneViewport viewport;
        const Extent extent{800, 600};
        auto draw = [&]()
        {
            viewport.begin_frame();
            ImGui::SetNextWindowPos(ImVec2(200, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            viewport.draw(world, selection, history);
            ImGui::Render();
        };
        std::vector<SceneView> views;
        viewport.build_scene_views(world, views, extent);
        const Vector3 initial_position = views.front().camera_position();
        io.AddMousePosEvent(500, 300);
        ImGui::NewFrame();
        draw();
        io.AddMouseWheelEvent(0.0f, 1.0f);
        ImGui::NewFrame();
        draw();
        views.clear();
        viewport.build_scene_views(world, views, extent);
        check(length(Vector3(0, 0, 300) - views.front().camera_position()) <
                  length(Vector3(0, 0, 300) - initial_position),
              "Mouse wheel over the Scene image must zoom toward the observer target");
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        ImGui::NewFrame();
        draw();
        const Vector3 direction_before_orbit = views.front().camera_direction();
        io.AddMousePosEvent(550, 320);
        ImGui::NewFrame();
        draw();
        views.clear();
        viewport.build_scene_views(world, views, extent);
        check(views.front().camera_direction() != direction_before_orbit,
              "Right dragging the Scene image must orbit the observer");
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        ImGui::NewFrame();
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
        ImGui::NewFrame();
        draw();
        const Vector3 position_before_pan = views.front().camera_position();
        const Vector3 direction_before_pan = views.front().camera_direction();
        io.AddMousePosEvent(580, 340);
        ImGui::NewFrame();
        draw();
        views.clear();
        viewport.build_scene_views(world, views, extent);
        check(views.front().camera_position() != position_before_pan &&
                  views.front().camera_direction() == direction_before_pan,
              "Middle dragging the Scene image must pan without rotating");
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        ImGui::NewFrame();
        draw();
        ImGui::DestroyContext(context);
    }
    return failures == 0 ? 0 : 1;
}
