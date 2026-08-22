#include "gamescene/actor/actor.h"
#include "gamescene/actor/static_mesh_actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/world/world.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"

#include <cmath>
#include <iostream>
#include <utility>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    bool nearly_equal(float left, float right)
    {
        return std::abs(left - right) <= 1.0e-5f;
    }

    class TrackingComponent final : public toy3d::ActorComponent
    {
    public:
        TrackingComponent(
            toy3d::Actor& owner,
            int& register_count,
            int& unregister_count)
            : ActorComponent(owner),
              register_count_(register_count),
              unregister_count_(unregister_count)
        {
        }

    protected:
        void on_register() override { ++register_count_; }
        void on_unregister() override { ++unregister_count_; }

    private:
        int& register_count_;
        int& unregister_count_;
    };

    toy3d::StaticMeshRef make_mesh()
    {
        toy3d::MaterialDesc material_desc;
        material_desc.shader_name = "Builtin/Surface/Phong";
        const toy3d::MaterialInstanceRef material =
            toy3d::MaterialInstance::create(
                toy3d::Material::create(std::move(material_desc)));

        toy3d::StaticMeshDesc mesh_desc;
        mesh_desc.vertices = {
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
        mesh_desc.indices = std::vector<std::uint16_t>{0, 1, 2};
        mesh_desc.sections.push_back({0, 3, 0});
        mesh_desc.material_slots.push_back(material);
        return toy3d::StaticMesh::create(std::move(mesh_desc));
    }
}

int main()
{
    using namespace toy3d;

    World world;
    Actor& parent_actor = world.spawn_actor();
    Actor& child_actor = world.spawn_actor();
    check(world.actor_count() == 2 && world.contains(parent_actor) &&
            parent_actor.is_registered(),
        "World::spawn_actor must own and register every spawned Actor");

    SceneComponent& parent = parent_actor.create_component<SceneComponent>();
    SceneComponent& child = child_actor.create_component<SceneComponent>();
    check(parent.is_registered() && child.is_registered(),
        "A component created on a registered Actor must register immediately");
    check(parent_actor.set_root_component(&parent) &&
            child_actor.set_root_component(&child),
        "An Actor must accept one of its own SceneComponents as root");
    check(!parent_actor.set_root_component(&child),
        "An Actor must reject a root component owned by another Actor");

    Transform parent_transform;
    parent_transform.translation = {10.0f, 0.0f, 0.0f};
    Transform child_transform;
    child_transform.translation = {0.0f, 2.0f, 0.0f};
    check(parent.set_local_transform(parent_transform) &&
            child.set_local_transform(child_transform) &&
            child.attach_to(&parent, AttachmentRule::KeepRelative),
        "A same-World component hierarchy must accept valid relative transforms");
    check(nearly_equal(child.world_transform().at(3, 0), 10.0f) &&
            nearly_equal(child.world_transform().at(3, 1), 2.0f),
        "Attachment must update the child world transform without a World scan");

    parent_transform.translation.x = 20.0f;
    check(parent.set_local_transform(parent_transform) &&
            nearly_equal(child.world_transform().at(3, 0), 20.0f),
        "A parent transform change must immediately propagate to descendants");
    check(!parent.attach_to(&child, AttachmentRule::KeepRelative),
        "SceneComponent attachment must reject cycles");

    const Matrix4 world_before_detach = child.world_transform();
    check(child.attach_to(nullptr, AttachmentRule::KeepWorld) &&
            is_nearly_equal(child.world_transform(), world_before_detach, 1.0e-5f),
        "KeepWorld detach must preserve a representable world transform");

    World other_world;
    SceneComponent& other_component =
        other_world.spawn_actor().create_component<SceneComponent>();
    check(!child.attach_to(&other_component, AttachmentRule::KeepRelative),
        "SceneComponent attachment must reject cross-World parents");

    int register_count = 0;
    int unregister_count = 0;
    Actor& lifecycle_actor = world.spawn_actor();
    lifecycle_actor.create_component<TrackingComponent>(
        register_count, unregister_count);
    check(register_count == 1 && unregister_count == 0,
        "ActorComponent registration must run exactly once");
    check(world.destroy_actor(lifecycle_actor) && unregister_count == 1,
        "World::destroy_actor must unregister components before destruction");

    StaticMeshActor& mesh_actor = world.spawn_actor<StaticMeshActor>();
    check(mesh_actor.root_component() == &mesh_actor.static_mesh_component() &&
            mesh_actor.static_mesh_component().is_registered(),
        "StaticMeshActor must own a registered StaticMeshComponent root");
    const StaticMeshRef mesh = make_mesh();
    check(mesh != nullptr, "The GameScene test mesh must be valid");
    mesh_actor.static_mesh_component().set_static_mesh(mesh);
    Transform mesh_transform;
    mesh_transform.translation.x = 4.0f;
    mesh_transform.scale = Vector3(2.0f);
    check(mesh_actor.static_mesh_component().set_local_transform(mesh_transform) &&
            nearly_equal(
                mesh_actor.static_mesh_component().world_bounds().minimum.x,
                2.0f) &&
            nearly_equal(
                mesh_actor.static_mesh_component().world_bounds().maximum.x,
                6.0f),
        "Primitive bounds must follow component transforms without World collection");

    CameraComponent& camera =
        world.spawn_actor().create_component<CameraComponent>();
    check(camera.set_perspective(75.0f, 0.25f, 500.0f) &&
            !camera.set_perspective(180.0f, 0.25f, 500.0f) &&
            nearly_equal(camera.vertical_fov_degrees(), 75.0f),
        "CameraComponent must retain valid camera properties without building ViewportFrame");

    PointLightComponent& point =
        world.spawn_actor().create_component<PointLightComponent>();
    check(point.set_range(20.0f) && !point.set_range(0.0f) &&
            nearly_equal(point.range(), 20.0f),
        "LightComponent validation must remain in the GameScene domain");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " GameScene test(s) failed.\n";
        return 1;
    }
    std::cout << "GameScene tests passed.\n";
    return 0;
}
