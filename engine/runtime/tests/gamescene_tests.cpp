#include "gamescene/render_id.h"
#include "gamescene/world.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "rendercore/geometry/static_mesh.h"
#include "rendercore/material/material.h"

#include "glm/gtc/matrix_transform.hpp"

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

    bool matrices_nearly_equal(const toy3d::mat4x4& lhs, const toy3d::mat4x4& rhs)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!nearly_equal(lhs[column][row], rhs[column][row]))
                {
                    return false;
                }
            }
        }
        return true;
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

    SceneTransform parent_transform;
    parent_transform.translation = vec3(10.0f, 0.0f, 0.0f);
    check(static_cast<bool>(parent.set_local_transform(parent_transform)),
        "A valid positive-scale parent transform must be accepted");

    SceneTransform child_transform;
    child_transform.translation = vec3(0.0f, 2.0f, 0.0f);
    check(static_cast<bool>(child.set_local_transform(child_transform)),
        "A valid child transform must be accepted");
    check(static_cast<bool>(child.attach_to(&parent, AttachmentRule::KeepRelative)),
        "Same-World KeepRelative attachment must succeed");

    world.update_transforms();
    check(nearly_equal(child.world_transform()[3].x, 10.0f) &&
        nearly_equal(child.world_transform()[3].y, 2.0f),
        "Child world transform must equal parent world times local transform");

    parent_transform.translation.x = 20.0f;
    check(static_cast<bool>(parent.set_local_transform(parent_transform)),
        "Updating a parent transform must succeed");
    check(parent.is_transform_dirty() && child.is_transform_dirty(),
        "A parent transform change must dirty all descendants");
    world.update_transforms();
    check(nearly_equal(child.world_transform()[3].x, 20.0f),
        "World transform update must propagate the changed parent transform");

    check(!parent.attach_to(&child, AttachmentRule::KeepRelative),
        "Attachment cycles must fail");

    const mat4x4 child_world_before_detach = child.world_transform();
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

    SceneTransform invalid_scale;
    invalid_scale.scale.x = 0.0f;
    check(!child.set_local_transform(invalid_scale),
        "Zero scale must fail");

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
        material_instance->revision() == 1,
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

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "GameScene tests passed\n";
    return 0;
}
