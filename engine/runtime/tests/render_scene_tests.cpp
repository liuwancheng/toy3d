#include "renderscene/scene/render_scene.h"

#include <cmath>
#include <iostream>
#include <limits>

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

    toy3d::RenderDirtyFlags all_dirty_flags()
    {
        return toy3d::RenderDirtyFlags::Transform |
            toy3d::RenderDirtyFlags::State |
            toy3d::RenderDirtyFlags::DynamicData;
    }

    toy3d::PrimitiveRenderSnapshot make_primitive_snapshot(
        std::uint64_t mesh_id,
        float translation_x)
    {
        toy3d::PrimitiveRenderSnapshot snapshot;
        snapshot.world_transform.at(3, 0) = translation_x;
        snapshot.world_bounds.minimum = {translation_x - 1.0f, -1.0f, -1.0f};
        snapshot.world_bounds.maximum = {translation_x + 1.0f, 1.0f, 1.0f};
        snapshot.mesh_resource_id = toy3d::MeshRenderResourceId(mesh_id);
        snapshot.material_resource_ids.push_back(
            toy3d::MaterialRenderResourceId(mesh_id + 100));
        return snapshot;
    }

    toy3d::LightRenderSnapshot make_point_light_snapshot(float translation_z)
    {
        toy3d::LightRenderSnapshot snapshot;
        snapshot.type = toy3d::LightType::Point;
        snapshot.world_transform.at(3, 2) = translation_z;
        snapshot.color = {1.0f, 0.5f, 0.25f};
        snapshot.intensity = 2.0f;
        snapshot.range = 20.0f;
        snapshot.render_priority = 3;
        return snapshot;
    }
}

int main()
{
    using namespace toy3d;

    const RenderSceneId scene_id(1);
    const PrimitiveId primitive_id(11);
    const LightId light_id(21);
    RenderScene scene(scene_id);

    RenderSceneUpdateBatch initial_batch;
    initial_batch.scene_id = scene_id;
    PrimitiveSceneUpdate primitive_add;
    primitive_add.operation = RenderSceneUpdateOperation::Add;
    primitive_add.dirty_flags = all_dirty_flags();
    primitive_add.primitive_id = primitive_id;
    primitive_add.snapshot = make_primitive_snapshot(31, 2.0f);
    initial_batch.primitive_updates.push_back(primitive_add);
    LightSceneUpdate light_add;
    light_add.operation = RenderSceneUpdateOperation::Add;
    light_add.dirty_flags = all_dirty_flags();
    light_add.light_id = light_id;
    light_add.snapshot = make_point_light_snapshot(4.0f);
    initial_batch.light_updates.push_back(light_add);

    const RenderSceneApplyResult initial_result = scene.apply_updates(initial_batch);
    check(initial_result.succeeded() && initial_result.added_count == 2 &&
        scene.primitive_count() == 1 && scene.light_count() == 1,
        "A valid Add batch must create persistent Primitive and Light scene entries");
    check(scene.find_primitive(primitive_id) != nullptr &&
        scene.find_light(light_id) != nullptr,
        "Persistent scene entries must be queryable by their strong Render IDs");

    RenderSceneUpdateBatch dynamic_batch;
    dynamic_batch.scene_id = scene_id;
    PrimitiveSceneUpdate primitive_transform;
    primitive_transform.operation = RenderSceneUpdateOperation::Update;
    primitive_transform.dirty_flags = RenderDirtyFlags::Transform;
    primitive_transform.primitive_id = primitive_id;
    primitive_transform.snapshot = make_primitive_snapshot(999, 8.0f);
    dynamic_batch.primitive_updates.push_back(primitive_transform);
    LightSceneUpdate light_dynamic;
    light_dynamic.operation = RenderSceneUpdateOperation::Update;
    light_dynamic.dirty_flags =
        RenderDirtyFlags::Transform | RenderDirtyFlags::DynamicData;
    light_dynamic.light_id = light_id;
    light_dynamic.snapshot = make_point_light_snapshot(9.0f);
    light_dynamic.snapshot.intensity = 6.0f;
    dynamic_batch.light_updates.push_back(light_dynamic);

    const RenderSceneApplyResult dynamic_result = scene.apply_updates(dynamic_batch);
    const PrimitiveRenderSnapshot& transformed_primitive =
        scene.find_primitive(primitive_id)->proxy().snapshot();
    const LightRenderSnapshot& transformed_light =
        scene.find_light(light_id)->proxy().snapshot();
    check(dynamic_result.succeeded() && dynamic_result.updated_count == 2,
        "Transform and Light DynamicData updates must apply without rebuilding identity");
    check(nearly_equal(
            transformed_primitive.world_transform.at(3, 0), 8.0f) &&
        transformed_primitive.mesh_resource_id == MeshRenderResourceId(31),
        "A transform-only Primitive update must preserve state owned by its Proxy");
    check(nearly_equal(transformed_light.world_transform.at(3, 2), 9.0f) &&
        nearly_equal(transformed_light.intensity, 6.0f) &&
        transformed_light.type == LightType::Point,
        "A Light update must merge transform and dynamic fields into its persistent Proxy");

    RenderSceneUpdateBatch unsupported_dynamic_batch;
    unsupported_dynamic_batch.scene_id = scene_id;
    PrimitiveSceneUpdate primitive_dynamic = primitive_transform;
    primitive_dynamic.dirty_flags = RenderDirtyFlags::DynamicData;
    unsupported_dynamic_batch.primitive_updates.push_back(primitive_dynamic);
    LightSceneUpdate spoofed_light_dynamic;
    spoofed_light_dynamic.operation = RenderSceneUpdateOperation::Update;
    spoofed_light_dynamic.dirty_flags = RenderDirtyFlags::DynamicData;
    spoofed_light_dynamic.light_id = light_id;
    spoofed_light_dynamic.snapshot.type = LightType::Directional;
    spoofed_light_dynamic.snapshot.intensity = 7.0f;
    unsupported_dynamic_batch.light_updates.push_back(spoofed_light_dynamic);
    const RenderSceneApplyResult unsupported_dynamic_result =
        scene.apply_updates(unsupported_dynamic_batch);
    check(!unsupported_dynamic_result.succeeded() &&
        unsupported_dynamic_result.rejected_count == 2 &&
        nearly_equal(
            scene.find_primitive(primitive_id)->proxy().snapshot().world_transform.at(3, 0),
            8.0f) &&
        nearly_equal(scene.find_light(light_id)->proxy().snapshot().intensity, 6.0f),
        "Unsupported Primitive data and spoofed Light types must not become no-op successes");

    RenderSceneUpdateBatch state_batch;
    state_batch.scene_id = scene_id;
    PrimitiveSceneUpdate primitive_state;
    primitive_state.operation = RenderSceneUpdateOperation::Update;
    primitive_state.dirty_flags = RenderDirtyFlags::State;
    primitive_state.primitive_id = primitive_id;
    primitive_state.snapshot = make_primitive_snapshot(41, 12.0f);
    state_batch.primitive_updates.push_back(primitive_state);
    check(scene.apply_updates(state_batch).succeeded() &&
        scene.find_primitive(primitive_id)->proxy().snapshot().mesh_resource_id ==
            MeshRenderResourceId(41) &&
        nearly_equal(
            scene.find_primitive(primitive_id)->proxy().snapshot().world_transform.at(3, 0),
            12.0f),
        "A Primitive State update must atomically replace its complete Proxy snapshot");

    RenderSceneUpdateBatch invalid_batch;
    invalid_batch.scene_id = scene_id;
    PrimitiveSceneUpdate invalid_state = primitive_state;
    invalid_state.snapshot.mesh_resource_id = MeshRenderResourceId{};
    invalid_batch.primitive_updates.push_back(invalid_state);
    PrimitiveSceneUpdate continuing_add;
    continuing_add.operation = RenderSceneUpdateOperation::Add;
    continuing_add.dirty_flags = all_dirty_flags();
    continuing_add.primitive_id = PrimitiveId(12);
    continuing_add.snapshot = make_primitive_snapshot(51, 0.0f);
    invalid_batch.primitive_updates.push_back(continuing_add);
    LightSceneUpdate unknown_remove;
    unknown_remove.operation = RenderSceneUpdateOperation::Remove;
    unknown_remove.light_id = LightId(22);
    invalid_batch.light_updates.push_back(unknown_remove);

    const RenderSceneApplyResult invalid_result = scene.apply_updates(invalid_batch);
    check(!invalid_result.succeeded() && invalid_result.rejected_count == 2 &&
        invalid_result.added_count == 1 && scene.primitive_count() == 2,
        "Object-level protocol errors must be diagnosed without stopping later batch updates");
    check(scene.find_primitive(primitive_id)->proxy().snapshot().mesh_resource_id ==
        MeshRenderResourceId(41),
        "A rejected State update must leave the existing Proxy unchanged");

    RenderSceneUpdateBatch duplicate_batch;
    duplicate_batch.scene_id = scene_id;
    duplicate_batch.primitive_updates.push_back(continuing_add);
    continuing_add.operation = RenderSceneUpdateOperation::Update;
    continuing_add.dirty_flags = RenderDirtyFlags::Transform;
    duplicate_batch.primitive_updates.push_back(continuing_add);
    const RenderSceneApplyResult duplicate_result = scene.apply_updates(duplicate_batch);
    check(!duplicate_result.succeeded() && duplicate_result.rejected_count == 2 &&
        duplicate_result.diagnostics[0].error == RenderSceneApplyError::DuplicateAdd &&
        duplicate_result.diagnostics[1].error ==
            RenderSceneApplyError::DuplicateUpdateInBatch,
        "Duplicate Add and repeated IDs in one batch must produce stable protocol diagnostics");

    RenderSceneUpdateBatch invalid_scene_batch;
    invalid_scene_batch.scene_id = RenderSceneId(2);
    continuing_add.operation = RenderSceneUpdateOperation::Add;
    continuing_add.dirty_flags = all_dirty_flags();
    continuing_add.primitive_id = PrimitiveId(13);
    invalid_scene_batch.primitive_updates.push_back(continuing_add);
    const RenderSceneApplyResult invalid_scene_result =
        scene.apply_updates(invalid_scene_batch);
    check(!invalid_scene_result.succeeded() &&
        invalid_scene_result.diagnostics.size() == 1 &&
        invalid_scene_result.diagnostics[0].error == RenderSceneApplyError::InvalidScene &&
        scene.find_primitive(PrimitiveId(13)) == nullptr,
        "A batch for another RenderScene must be rejected before mutating this scene");

    RenderSceneUpdateBatch removal_batch;
    removal_batch.scene_id = scene_id;
    PrimitiveSceneUpdate primitive_remove;
    primitive_remove.operation = RenderSceneUpdateOperation::Remove;
    primitive_remove.primitive_id = primitive_id;
    removal_batch.primitive_updates.push_back(primitive_remove);
    LightSceneUpdate light_remove;
    light_remove.operation = RenderSceneUpdateOperation::Remove;
    light_remove.light_id = light_id;
    removal_batch.light_updates.push_back(light_remove);
    const RenderSceneApplyResult removal_result = scene.apply_updates(removal_batch);
    check(removal_result.succeeded() && removal_result.removed_count == 2 &&
        scene.find_primitive(primitive_id) == nullptr &&
        scene.find_light(light_id) == nullptr,
        "Valid Remove updates must destroy their persistent Info and Proxy entries");

    RenderSceneUpdateBatch invalid_light_batch;
    invalid_light_batch.scene_id = scene_id;
    LightSceneUpdate invalid_light_add = light_add;
    invalid_light_add.light_id = LightId(23);
    invalid_light_add.snapshot.intensity = std::numeric_limits<float>::infinity();
    invalid_light_batch.light_updates.push_back(invalid_light_add);
    check(!scene.apply_updates(invalid_light_batch).succeeded() &&
        scene.find_light(LightId(23)) == nullptr,
        "Non-finite Light payloads must not enter the persistent RenderScene");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RenderScene tests passed\n";
    return 0;
}
