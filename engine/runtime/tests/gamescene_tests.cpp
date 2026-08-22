#include "rendercore/render_id.h"
#include "gamescene/world.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"

#include <cmath>
#include <iostream>
#include <type_traits>

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

    bool nearly_equal(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 1.0e-5f;
    }

    bool matrices_nearly_equal(
        const toy3d::Matrix4& lhs,
        const toy3d::Matrix4& rhs)
    {
        return toy3d::is_nearly_equal(lhs, rhs, 1.0e-5f);
    }
}

int main()
{
    using namespace toy3d;

    static_assert(!std::is_same<PrimitiveId, LightId>::value,
        "Render IDs from different domains must not be interchangeable");
    check(!PrimitiveId{}, "A default Render ID must be invalid");
    const PrimitiveId primitive_a = allocate_render_id<PrimitiveId>();
    const PrimitiveId primitive_b = allocate_render_id<PrimitiveId>();
    check(primitive_a && primitive_b && primitive_a != primitive_b,
        "Render IDs must be valid and never reused");

    World world;
    Actor& parent_actor = world.create_actor();
    Actor& child_actor = world.create_actor();
    SceneComponent& parent = parent_actor.create_scene_component();
    SceneComponent& child = child_actor.create_scene_component();

    Transform parent_transform;
    parent_transform.translation = Vector3(10.0f, 0.0f, 0.0f);
    check(static_cast<bool>(parent.set_local_transform(parent_transform)),
        "A valid positive-scale parent transform must be accepted");

    Transform child_transform;
    child_transform.translation = Vector3(0.0f, 2.0f, 0.0f);
    check(static_cast<bool>(child.set_local_transform(child_transform)),
        "A valid child transform must be accepted");
    check(static_cast<bool>(child.attach_to(&parent, AttachmentRule::KeepRelative)),
        "Same-World KeepRelative attachment must succeed");

    world.update_transforms();
    check(nearly_equal(child.world_transform().at(3, 0), 10.0f) &&
        nearly_equal(child.world_transform().at(3, 1), 2.0f),
        "Child world transform must equal parent world times local transform");

    parent_transform.translation.x = 20.0f;
    check(static_cast<bool>(parent.set_local_transform(parent_transform)),
        "Updating a parent transform must succeed");
    check(parent.is_transform_dirty() && child.is_transform_dirty(),
        "A parent transform change must dirty all descendants");
    world.update_transforms();
    check(nearly_equal(child.world_transform().at(3, 0), 20.0f),
        "World transform update must propagate the changed parent transform");

    check(!parent.attach_to(&child, AttachmentRule::KeepRelative),
        "Attachment cycles must fail");

    const Matrix4 child_world_before_detach = child.world_transform();
    check(static_cast<bool>(child.attach_to(nullptr, AttachmentRule::KeepWorld)),
        "KeepWorld detach must succeed for a representable transform");
    world.update_transforms();
    check(matrices_nearly_equal(child_world_before_detach, child.world_transform()),
        "KeepWorld detach must preserve the component world transform");

    check(static_cast<bool>(child.attach_to(&parent, AttachmentRule::KeepWorld)),
        "KeepWorld attachment must succeed for a representable transform");
    world.update_transforms();
    check(matrices_nearly_equal(child_world_before_detach, child.world_transform()),
        "KeepWorld attachment must preserve the component world transform");

    World other_world;
    SceneComponent& other_component =
        other_world.create_actor().create_scene_component();
    check(!child.attach_to(&other_component, AttachmentRule::KeepRelative),
        "Cross-World attachment must be rejected");

    Transform invalid_scale;
    invalid_scale.scale.x = 0.0f;
    check(!child.set_local_transform(invalid_scale),
        "Zero scale must fail");

    SceneComponent& shear_parent =
        world.create_actor().create_scene_component();
    SceneComponent& shear_child =
        world.create_actor().create_scene_component();
    Transform shear_parent_transform;
    shear_parent_transform.scale = Vector3(2.0f, 1.0f, 1.0f);
    Transform shear_child_transform;
    check(try_make_quaternion_from_axis_angle(
            Vector3(0.0f, 0.0f, 1.0f),
            to_radians(Degrees(45.0f)),
            shear_child_transform.rotation) &&
        shear_parent.set_local_transform(shear_parent_transform) &&
        shear_child.set_local_transform(shear_child_transform) &&
        shear_child.attach_to(&shear_parent, AttachmentRule::KeepRelative),
        "The hierarchy shear test must build a valid relative attachment");
    world.update_transforms();
    const Matrix4 sheared_world = shear_child.world_transform();
    check(!shear_child.attach_to(nullptr, AttachmentRule::KeepWorld) &&
            shear_child.parent() == &shear_parent &&
            matrices_nearly_equal(
                shear_child.world_transform(), sheared_world),
        "KeepWorld must reject shear atomically while KeepRelative preserves its world matrix");

    check(!static_cast<bool>(parent_actor.set_root_component(&child)),
        "An Actor must reject a root component owned by another Actor");

    MaterialDesc material_desc;
    material_desc.shader_name = "Builtin/Surface/Phong";
    const MaterialRef material = Material::create(material_desc);
    check(material != nullptr &&
        material->desc().shading_model == MaterialShadingModel::Phong &&
        material->desc().blend_mode == MaterialBlendMode::Opaque,
        "A Material must preserve immutable shader and render-state identity");
    const MaterialInstanceRef material_instance =
        MaterialInstance::create(material);
    check(material_instance != nullptr &&
        material_instance->material() == material &&
        material_instance->revision() == RenderResourceRevision(1),
        "A MaterialInstance must strongly reference its immutable Material");
    check(Material::create({}) == nullptr,
        "A Material without ShaderMap identity must fail");
    check(MaterialInstance::create(nullptr) == nullptr,
        "A MaterialInstance without a Material must fail");

    StaticMeshDesc mesh_desc;
    mesh_desc.vertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
    mesh_desc.indices = std::vector<std::uint16_t>{0, 1, 2};
    mesh_desc.sections.push_back({0, 3, 0});
    mesh_desc.material_slots.push_back(material_instance);
    const StaticMeshRef mesh = StaticMesh::create(std::move(mesh_desc));
    check(mesh != nullptr && mesh->sections().size() == 1,
        "A valid immutable StaticMesh CPU asset must be created");
    check(mesh->revision() == RenderResourceRevision(1),
        "An immutable StaticMesh must expose its initial strong resource revision");
    check(nearly_equal(mesh->local_bounds().minimum.x, -1.0f) &&
        nearly_equal(mesh->local_bounds().maximum.y, 1.0f),
        "StaticMesh local bounds must be derived from vertex positions");

    Actor& render_actor = world.create_actor();
    StaticMeshComponent& mesh_component =
        render_actor.create_scene_component<StaticMeshComponent>();
    mesh_component.set_static_mesh(mesh);
    check(mesh_component.material_for_slot(0) == material_instance,
        "A StaticMeshComponent must resolve its mesh Material slot");

    MaterialDesc translucent_desc;
    translucent_desc.shader_name = "Builtin/Surface/Phong";
    translucent_desc.blend_mode = MaterialBlendMode::Translucent;
    const MaterialInstanceRef translucent_instance = MaterialInstance::create(
        Material::create(std::move(translucent_desc)));
    check(mesh_component.set_material_override(0, translucent_instance) &&
        mesh_component.material_for_slot(0) == translucent_instance,
        "A component Material override must replace only the selected slot");
    check(!mesh_component.set_material_override(1, translucent_instance),
        "A component Material override outside the mesh slots must fail");

    CameraComponent& camera =
        world.create_actor().create_scene_component<CameraComponent>();
    check(camera.projection_mode() == CameraProjectionMode::Perspective &&
        nearly_equal(camera.vertical_fov_degrees(), 60.0f) &&
        nearly_equal(camera.near_clip(), 0.1f) &&
        nearly_equal(camera.far_clip(), 1000.0f),
        "A CameraComponent must use the confirmed finite perspective defaults");
    check(camera.set_perspective(75.0f, 0.25f, 500.0f),
        "A valid finite perspective camera must be accepted");
    check(!camera.set_perspective(180.0f, 0.25f, 500.0f) &&
        nearly_equal(camera.vertical_fov_degrees(), 75.0f),
        "Invalid camera parameters must fail atomically");

    DirectionalLightComponent& directional =
        world.create_actor().create_scene_component<DirectionalLightComponent>();
    check(directional.set_color({1.0f, 0.8f, 0.6f}) &&
        directional.set_intensity(2.0f),
        "Directional light linear color and intensity must accept valid values");
    check(!directional.set_color({-1.0f, 0.0f, 0.0f}) &&
        !directional.set_intensity(-1.0f),
        "Light color and intensity must reject negative values");

    PointLightComponent& point =
        world.create_actor().create_scene_component<PointLightComponent>();
    check(point.set_range(20.0f) && nearly_equal(point.range(), 20.0f),
        "A PointLight must accept a positive range");
    check(!point.set_range(0.0f) && nearly_equal(point.range(), 20.0f),
        "A PointLight must reject a non-positive range atomically");

    SpotLightComponent& spot =
        world.create_actor().create_scene_component<SpotLightComponent>();
    check(spot.set_cone_angles(15.0f, 35.0f),
        "A SpotLight must accept ordered cone angles below 90 degrees");
    check(!spot.set_cone_angles(40.0f, 30.0f) &&
        nearly_equal(spot.inner_angle_degrees(), 15.0f) &&
        nearly_equal(spot.outer_angle_degrees(), 35.0f),
        "Invalid SpotLight cone angles must fail atomically");

    const RenderSceneUpdateBatch initial_updates =
        world.collect_render_scene_updates();
    check(initial_updates.scene_id == world.render_scene_id() &&
        initial_updates.primitive_updates.size() == 1 &&
        initial_updates.light_updates.size() == 3,
        "The first collection must register every renderable component exactly once");
    check(initial_updates.primitive_updates[0].operation ==
            RenderSceneUpdateOperation::Add &&
        initial_updates.primitive_updates[0].primitive_id ==
            mesh_component.primitive_id() &&
        has_render_dirty_flag(
            initial_updates.primitive_updates[0].dirty_flags,
            RenderDirtyFlags::Transform) &&
        has_render_dirty_flag(
            initial_updates.primitive_updates[0].dirty_flags,
            RenderDirtyFlags::State) &&
        initial_updates.primitive_updates[0].snapshot.mesh_resource_id ==
            mesh->render_resource_id() &&
        initial_updates.primitive_updates[0].snapshot.material_resource_ids.size() == 1 &&
        initial_updates.primitive_updates[0].snapshot.material_resource_ids[0] ==
            translucent_instance->render_resource_id(),
        "A Primitive Add must own its complete transform and resource identity snapshot");
    check(nearly_equal(mesh_component.world_bounds().minimum.x, -1.0f) &&
        nearly_equal(mesh_component.world_bounds().maximum.y, 1.0f),
        "Frame-end collection must update StaticMesh world bounds");

    Transform moved_mesh_transform;
    moved_mesh_transform.translation.x = 4.0f;
    moved_mesh_transform.scale = Vector3(2.0f);
    check(mesh_component.set_local_transform(moved_mesh_transform),
        "A moved Primitive transform must be accepted");
    check(mesh_component.set_material_override(0, material_instance),
        "A Primitive state change must be accepted before update collection");

    Transform moved_light_transform;
    moved_light_transform.translation.z = 3.0f;
    check(directional.set_local_transform(moved_light_transform) &&
        directional.set_intensity(4.0f),
        "Light transform and dynamic data changes must be accepted");

    const RenderSceneUpdateBatch merged_updates =
        world.collect_render_scene_updates();
    check(merged_updates.primitive_updates.size() == 1 &&
        merged_updates.primitive_updates[0].operation ==
            RenderSceneUpdateOperation::Update &&
        has_render_dirty_flag(
            merged_updates.primitive_updates[0].dirty_flags,
            RenderDirtyFlags::Transform) &&
        has_render_dirty_flag(
            merged_updates.primitive_updates[0].dirty_flags,
            RenderDirtyFlags::State),
        "Transform and state changes must merge into one Primitive update per frame");
    check(nearly_equal(mesh_component.world_bounds().minimum.x, 2.0f) &&
        nearly_equal(mesh_component.world_bounds().maximum.x, 6.0f) &&
        nearly_equal(mesh_component.world_bounds().minimum.y, -2.0f) &&
        nearly_equal(mesh_component.world_bounds().maximum.y, 2.0f),
        "World bounds must include positive non-uniform component transforms");
    check(merged_updates.light_updates.size() == 1 &&
        merged_updates.light_updates[0].light_id == directional.light_id() &&
        has_render_dirty_flag(
            merged_updates.light_updates[0].dirty_flags,
            RenderDirtyFlags::Transform) &&
        has_render_dirty_flag(
            merged_updates.light_updates[0].dirty_flags,
            RenderDirtyFlags::DynamicData),
        "Transform and dynamic changes must merge into one Light update per frame");
    check(world.collect_render_scene_updates().empty(),
        "A collected World with no further changes must emit no updates");

    const PrimitiveId removed_primitive_id = mesh_component.primitive_id();
    moved_mesh_transform.translation.x = 8.0f;
    check(mesh_component.set_local_transform(moved_mesh_transform),
        "A registered Primitive may become dirty before destruction");
    check(world.destroy_actor(render_actor),
        "Destroying an Actor owned by the World must succeed");
    const RenderSceneUpdateBatch removal_updates =
        world.collect_render_scene_updates();
    check(removal_updates.primitive_updates.size() == 1 &&
        removal_updates.primitive_updates[0].operation ==
            RenderSceneUpdateOperation::Remove &&
        removal_updates.primitive_updates[0].primitive_id == removed_primitive_id,
        "Removing a registered Primitive must override prior dirty state with Remove");

    Actor& transient_actor = world.create_actor();
    transient_actor.create_scene_component<StaticMeshComponent>().set_static_mesh(mesh);
    check(world.destroy_actor(transient_actor) &&
        world.collect_render_scene_updates().empty(),
        "A renderable Actor created and destroyed before collection must emit no update");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "GameScene tests passed\n";
    return 0;
}
