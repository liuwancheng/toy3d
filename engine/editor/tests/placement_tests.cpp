#include "commands/editor_command_history.h"
#include "placement/actor_placement.h"
#include "viewport/actor_icons.h"
#include "viewport/scene_viewport.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <limits>
#include <vector>

#include "imgui.h"

#include "gamescene/actor/light_actor.h"
#include "gamescene/actor/camera_actor.h"
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
                                        toy3d::AxisAlignedBounds, bool) override {}
        void remove_primitive(toy3d::PrimitiveSceneProxy*) override {}
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
        check(actor->light_component().set_intensity(5) && actor->light_component().set_range(12), "Light property gesture");
        history.finish(world, EditorTransformSource::Details);
        check(history.undo(world) && actor->light_component().intensity() == 1 && actor->light_component().range() == 10,
              "Undo must restore all light properties in one gesture");
        check(history.redo(world) && actor->light_component().intensity() == 5, "Redo must restore edited light values");
        check(history.delete_actor(world, id) && history.undo(world), "Light deletion must reconstruct a typed Actor");
        actor = dynamic_cast<PointLightActor*>(world.find_actor_by_id(world.actor_ids().front()));
        check(actor && actor->light_component().range() == 12 && actor->light_component().intensity() == 5,
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
            EditorActorState invalid = capture_actor_state(*actor);
            invalid.transform.translation = Vector3(99);
            invalid.camera_near_clip = invalid.camera_far_clip;
            check(!apply_actor_state(*actor, invalid) && actor->root_component()->local_transform().translation ==
                  request.transform.translation, "Invalid projection must not partially apply its Transform");
            const float bad_values[] = {0, 180, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::infinity(),
                                        std::numeric_limits<float>::quiet_NaN()};
            for (const float value : bad_values)
                check(!actor->camera_component().set_perspective(value, 0.1f, 100) &&
                      actor->camera_component().vertical_fov_degrees() == 75, "Invalid FOV must preserve camera settings");
            invalid = capture_actor_state(*actor);
            invalid.transform.translation = Vector3(99);
            invalid.camera_near_clip = 1e30f;
            invalid.camera_far_clip = 2e30f;
            check(!apply_actor_state(*actor, invalid) && actor->root_component()->local_transform().translation ==
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
        ActorFactory factory;
        check(factory.initialize(), "Builtin mesh creation must use the compiled shader schema");
        World world;
        PlacementRequest request;
        request.item = PlacementItemId::Cube;
        check(factory.create(world, request) != nullptr, "Cube geometry should be reusable");
        request.item = PlacementItemId::Plane;
        check(factory.create(world, request) != nullptr, "Plane geometry should be reusable");
        for (auto id : world.actor_ids()) check(world.destroy_actor(*world.find_actor_by_id(id)), "Geometry Actor cleanup");
        factory.release();
    }
    {
        Matrix4 view;
        Matrix4 projection;
        Quaternion rotation;
        const Vector3 camera(0, 5, -5);
        check(try_make_rotation_from_forward_up(Vector3(0, -1, 1), Vector3(0, 1, 0), rotation) &&
              try_make_view_matrix(camera, rotation, view), "Placement camera construction");
        PerspectiveProjectionDesc desc;
        desc.vertical_fov = to_radians(Degrees(60));
        desc.aspect = 1;
        desc.near_clip = 0.1f;
        desc.far_clip = 1000;
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
        transform.translation = Vector3(0, 0, 2);
        check(near_light.root_component()->set_local_transform(transform), "Near icon transform");
        transform.translation.z = 5;
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
        camera_transform.translation = Vector3(0, 0, 1);
        check(camera.root_component()->set_local_transform(camera_transform), "Camera icon transform");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), camera.actor_id(), true, 2.0f) ==
              camera.actor_id(), "Camera and light icons must share frontmost picking");
        camera_transform.translation = Vector3(0, 0, 9);
        check(camera.root_component()->set_local_transform(camera_transform) &&
              draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), camera.actor_id(), true, 2.0f) ==
              near_light.actor_id(), "A farther camera must not steal a nearer light hit");
        camera_transform.translation = Vector3(0, 0, -2);
        check(camera.root_component()->set_local_transform(camera_transform), "Clipped camera icon transform");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), 0, true, 2.0f) ==
              near_light.actor_id(), "Overlapping light icons must select the frontmost Actor");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), 0, false, 2.0f) == 0,
              "Drag/drop or blocked image input must suppress icon hits");
        near_light.light_component().set_enabled(false);
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), near_light.actor_id(), true, 2.0f) ==
              near_light.actor_id(), "Disabled lights must remain selectable for editing");
        transform.translation = Vector3(0, 0, -2);
        check(near_light.root_component()->set_local_transform(transform), "Move near icon behind camera");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), 0, true, 2.0f) ==
              far_light.actor_id(), "A clipped light must not intercept another icon");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), far_light.actor_id(), true, 2.0f) ==
              far_light.actor_id(), "Selected directional-light depth cues must preserve icon hits");
        transform.translation = Vector3(0, 0, 5);
        check(try_make_rotation_from_forward_up(Vector3(1, -1, 1), Vector3(0, 1, 0), transform.rotation) &&
              far_light.root_component()->set_local_transform(transform), "Rotate the directional-light indicator");
        check(draw_actor_icons(world, projection, Vector2(100, 50), Vector2(800, 400), far_light.actor_id(), true, 2.0f) ==
              far_light.actor_id(), "Selected directional-light arrows must preserve icon hits after rotation");
        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext(context);
    }
    return failures == 0 ? 0 : 1;
}
