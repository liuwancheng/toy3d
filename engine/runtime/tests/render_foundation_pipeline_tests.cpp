#include "renderscene/render_scene_frame_processor.h"
#include "renderscene/resources/primitive_render_resources.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

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

    toy3d::MaterialInstanceRef make_material(
        toy3d::MaterialBlendMode blend_mode = toy3d::MaterialBlendMode::Opaque)
    {
        toy3d::MaterialDesc desc;
        desc.shader_name = "Builtin/Surface/Phong";
        desc.blend_mode = blend_mode;
        return toy3d::MaterialInstance::create(
            toy3d::Material::create(std::move(desc)));
    }

    toy3d::StaticMeshRef make_mesh(
        const toy3d::MaterialInstanceRef& material)
    {
        toy3d::StaticMeshDesc desc;
        desc.vertices = {
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}};
        desc.indices = std::vector<std::uint16_t>{0, 1, 2};
        desc.sections.push_back({0, 3, 0});
        desc.material_slots.push_back(material);
        return toy3d::StaticMesh::create(std::move(desc));
    }

    toy3d::MaterialRenderResourceVersionRef make_material_version(
        const toy3d::MaterialInstanceRef& material)
    {
        auto version = std::make_shared<toy3d::MaterialRenderResourceVersion>();
        version->resource_id = material->render_resource_id();
        version->revision = material->revision();
        version->material = material->material()->desc();
        return version;
    }

    toy3d::MeshRenderResourceVersionRef make_mesh_version(
        const toy3d::StaticMeshRef& mesh)
    {
        auto version = std::make_shared<toy3d::MeshRenderResourceVersion>();
        version->resource_id = mesh->render_resource_id();
        version->revision = mesh->revision();
        version->vertices = mesh->vertices();
        version->indices = mesh->indices();
        version->sections = mesh->sections();
        return version;
    }

    toy3d::TextureRenderResourceVersionRef make_placeholder_texture(
        std::uint64_t id,
        toy3d::TextureColorSemantic semantic)
    {
        auto texture = std::make_shared<toy3d::TextureRenderResourceVersion>();
        texture->resource_id = toy3d::TextureRenderResourceId(id);
        texture->revision = toy3d::RenderResourceRevision(1);
        texture->width = 1;
        texture->height = 1;
        texture->color_semantic = semantic;
        texture->rgba8_pixels = {255, 255, 255, 255};
        return texture;
    }

    toy3d::RenderResourcePlaceholders make_placeholders()
    {
        auto material = std::make_shared<toy3d::MaterialRenderResourceVersion>();
        material->resource_id = toy3d::MaterialRenderResourceId(9001);
        material->revision = toy3d::RenderResourceRevision(1);
        material->material.shader_name = "Builtin/Error";

        toy3d::RenderResourcePlaceholders placeholders;
        placeholders.error_material = std::move(material);
        placeholders.checkerboard_texture = make_placeholder_texture(
            9002, toy3d::TextureColorSemantic::Color);
        placeholders.white_texture = make_placeholder_texture(
            9003, toy3d::TextureColorSemantic::Linear);
        placeholders.normal_texture = make_placeholder_texture(
            9004, toy3d::TextureColorSemantic::Normal);
        return placeholders;
    }
}

int main()
{
    using namespace toy3d;

    const MaterialInstanceRef material = make_material();
    const StaticMeshRef mesh = make_mesh(material);
    check(material != nullptr && mesh != nullptr,
        "The test assets must satisfy the Game-side resource contract");

    const RenderSceneId scene_id(1);
    const PrimitiveId primitive_id(1);
    RenderFramePacket packet;
    packet.frame_id = RenderFrameId(1);
    MaterialRenderResourceUpdate material_update;
    material_update.resource_id = material->render_resource_id();
    material_update.version = make_material_version(material);
    packet.resource_updates.push_back(std::move(material_update));
    MeshRenderResourceUpdate mesh_update;
    mesh_update.resource_id = mesh->render_resource_id();
    mesh_update.version = make_mesh_version(mesh);
    packet.resource_updates.push_back(std::move(mesh_update));

    RenderSceneUpdateBatch initial_scene_update;
    initial_scene_update.scene_id = scene_id;
    PrimitiveSceneUpdate add_primitive;
    add_primitive.operation = RenderSceneUpdateOperation::Add;
    add_primitive.dirty_flags =
        RenderDirtyFlags::Transform |
        RenderDirtyFlags::State |
        RenderDirtyFlags::DynamicData;
    add_primitive.primitive_id = primitive_id;
    add_primitive.snapshot.world_transform = Matrix4::identity();
    add_primitive.snapshot.world_bounds = mesh->local_bounds();
    add_primitive.snapshot.mesh_resource_id = mesh->render_resource_id();
    add_primitive.snapshot.material_resource_ids.push_back(
        material->render_resource_id());
    initial_scene_update.primitive_updates.push_back(std::move(add_primitive));
    packet.scene_updates.push_back(std::move(initial_scene_update));
    SceneViewDesc scene_view_desc;
    scene_view_desc.view_rect = {0, 0, 1280, 720};
    SceneView scene_view;
    std::string scene_view_diagnostic;
    check(build_scene_view(
            scene_view_desc, scene_view, scene_view_diagnostic),
        "The pipeline test must build its owned SceneView snapshot");
    ViewportFrame viewport_frame;
    viewport_frame.viewport_id = ViewportId(1);
    SceneViewFamilyFrame family_frame;
    family_frame.view_family.scene_id = scene_id;
    family_frame.view_family.views.push_back(scene_view);
    family_frame.output.output_id = SceneOutputId(1);
    family_frame.output.extent = {1280, 720};
    viewport_frame.scene_frames.push_back(std::move(family_frame));
    packet.viewport_frames.push_back(std::move(viewport_frame));

    RenderSceneFrameProcessor processor(make_placeholders());
    check(static_cast<bool>(processor.initialize()),
        "A processor with valid placeholders must initialize");
    check(static_cast<bool>(processor.process_frame(packet)),
        "Valid resource and scene updates must be accepted in one frame");
    check(processor.last_report().resource_result.applied_count == 2 &&
        processor.last_report().scene_results.size() == 1 &&
        processor.last_report().scene_results[0].result.added_count == 1 &&
        processor.last_report().viewport_validation ==
            ViewportFrameValidation::Valid,
        "The real processor must apply resources before the packet's persistent Scene updates");

    const RenderScene* scene = processor.find_scene(scene_id);
    const PrimitiveSceneInfo* primitive =
        scene != nullptr ? scene->find_primitive(primitive_id) : nullptr;
    check(primitive != nullptr,
        "The processor must retain the World's persistent PrimitiveSceneInfo");
    PrimitiveRenderResources resolved;
    if (primitive != nullptr)
    {
        resolved = resolve_primitive_render_resources(
            primitive->proxy().snapshot(), processor.resource_cache());
    }
    check(resolved.is_ready() && resolved.mesh != nullptr &&
        resolved.mesh->resource_id == mesh->render_resource_id() &&
        resolved.materials.size() == 1 &&
        resolved.materials[0]->resource_id == material->render_resource_id() &&
        resolved.placeholder_material_count == 0,
        "Prepare resolution must retain the exact immutable Mesh and Material versions");

    RenderFramePacket missing_material_packet;
    missing_material_packet.frame_id = RenderFrameId(2);
    RenderSceneUpdateBatch missing_material_batch;
    missing_material_batch.scene_id = scene_id;
    PrimitiveSceneUpdate missing_material_update;
    missing_material_update.operation = RenderSceneUpdateOperation::Update;
    missing_material_update.dirty_flags =
        RenderDirtyFlags::Transform | RenderDirtyFlags::State;
    missing_material_update.primitive_id = primitive_id;
    missing_material_update.snapshot = primitive->proxy().snapshot();
    missing_material_update.snapshot.material_resource_ids[0] =
        MaterialRenderResourceId(9999);
    missing_material_batch.primitive_updates.push_back(
        std::move(missing_material_update));
    missing_material_packet.scene_updates.push_back(
        std::move(missing_material_batch));
    check(static_cast<bool>(processor.process_frame(missing_material_packet)),
        "A missing content resource must not become a fatal frame outcome");
    scene = processor.find_scene(scene_id);
    primitive = scene != nullptr ? scene->find_primitive(primitive_id) : nullptr;
    if (primitive != nullptr)
    {
        resolved = resolve_primitive_render_resources(
            primitive->proxy().snapshot(), processor.resource_cache());
    }
    check(resolved.is_ready() && resolved.placeholder_material_count == 1 &&
        resolved.materials[0]->material.shader_name == "Builtin/Error",
        "A missing Material identity must resolve to the configured Error Material version");

    PrimitiveRenderSnapshot invalid_slots_snapshot = primitive->proxy().snapshot();
    invalid_slots_snapshot.material_resource_ids.clear();
    const PrimitiveRenderResources invalid_slots =
        resolve_primitive_render_resources(
            invalid_slots_snapshot, processor.resource_cache());
    check(invalid_slots.state ==
            PrimitiveRenderResourceState::InvalidMaterialSlots,
        "Prepare resolution must reject Mesh sections without a matching Material slot");

    RenderFramePacket diagnostic_packet;
    diagnostic_packet.frame_id = RenderFrameId(3);
    MeshRenderResourceUpdate invalid_update;
    diagnostic_packet.resource_updates.push_back(invalid_update);
    check(static_cast<bool>(processor.process_frame(diagnostic_packet)) &&
        processor.last_report().has_diagnostics() &&
        processor.last_report().resource_result.rejected_count == 1,
        "Content diagnostics must be retained without aborting otherwise usable frame state");

    RenderFramePacket unsupported_view_packet;
    unsupported_view_packet.frame_id = RenderFrameId(4);
    ViewportFrame unsupported_viewport;
    unsupported_viewport.viewport_id = ViewportId(2);
    SceneViewFamilyFrame first_present;
    first_present.view_family.scene_id = scene_id;
    first_present.view_family.views.push_back(scene_view);
    first_present.output.output_id = SceneOutputId(2);
    first_present.output.extent = {1280, 720};
    SceneViewFamilyFrame second_present = first_present;
    second_present.output.output_id = SceneOutputId(3);
    unsupported_viewport.scene_frames.push_back(std::move(first_present));
    unsupported_viewport.scene_frames.push_back(std::move(second_present));
    unsupported_view_packet.viewport_frames.push_back(
        std::move(unsupported_viewport));
    RenderSceneUpdateBatch observation_failure_batch;
    observation_failure_batch.scene_id = scene_id;
    PrimitiveSceneUpdate remove_before_observation_failure;
    remove_before_observation_failure.operation =
        RenderSceneUpdateOperation::Remove;
    remove_before_observation_failure.primitive_id = primitive_id;
    observation_failure_batch.primitive_updates.push_back(
        std::move(remove_before_observation_failure));
    unsupported_view_packet.scene_updates.push_back(
        std::move(observation_failure_batch));
    const RenderFrameExecutionStatus unsupported_view_status =
        processor.process_frame(unsupported_view_packet);
    check(unsupported_view_status.outcome ==
            RenderFrameExecutionOutcome::FrameFailed &&
            processor.last_report().viewport_validation ==
                ViewportFrameValidation::Unsupported &&
            processor.last_report().has_diagnostics() &&
            !processor.last_report().viewport_diagnostic.empty(),
        "An unsupported viewport observation must fail only its frame with a retained diagnostic");
    scene = processor.find_scene(scene_id);
    check(scene != nullptr &&
            scene->find_primitive(primitive_id) == nullptr,
        "Persistent Scene updates must remain applied when later observation validation fails");

    RenderFramePacket unknown_scene_packet;
    unknown_scene_packet.frame_id = RenderFrameId(5);
    ViewportFrame unknown_scene_viewport;
    unknown_scene_viewport.viewport_id = ViewportId(3);
    SceneViewFamilyFrame unknown_scene_frame;
    unknown_scene_frame.view_family.scene_id = RenderSceneId(9999);
    unknown_scene_frame.view_family.views.push_back(scene_view);
    unknown_scene_frame.output.output_id = SceneOutputId(4);
    unknown_scene_frame.output.extent = {1280, 720};
    unknown_scene_viewport.scene_frames.push_back(
        std::move(unknown_scene_frame));
    unknown_scene_packet.viewport_frames.push_back(
        std::move(unknown_scene_viewport));
    const RenderFrameExecutionStatus unknown_scene_status =
        processor.process_frame(unknown_scene_packet);
    check(unknown_scene_status.outcome ==
            RenderFrameExecutionOutcome::FrameFailed &&
            processor.last_report().viewport_validation ==
                ViewportFrameValidation::InvalidArgument &&
            processor.last_report().viewport_diagnostic.find("unknown") !=
                std::string::npos,
        "A non-zero output cannot observe a RenderScene that the processor does not own");

    RenderFramePacket minimized_unknown_scene_packet;
    minimized_unknown_scene_packet.frame_id = RenderFrameId(6);
    ViewportFrame minimized_unknown_scene_viewport;
    minimized_unknown_scene_viewport.viewport_id = ViewportId(4);
    SceneViewFamilyFrame minimized_unknown_scene_frame;
    minimized_unknown_scene_frame.view_family.scene_id = RenderSceneId(9999);
    minimized_unknown_scene_frame.view_family.views.push_back(scene_view);
    minimized_unknown_scene_frame.output.output_id = SceneOutputId(5);
    minimized_unknown_scene_frame.output.extent = {};
    minimized_unknown_scene_viewport.scene_frames.push_back(
        std::move(minimized_unknown_scene_frame));
    minimized_unknown_scene_packet.viewport_frames.push_back(
        std::move(minimized_unknown_scene_viewport));
    check(static_cast<bool>(
            processor.process_frame(minimized_unknown_scene_packet)) &&
            processor.last_report().viewport_validation ==
                ViewportFrameValidation::Valid,
        "A zero-extent output must skip Scene lookup after validating its owned family");
    check(static_cast<bool>(processor.flush()) &&
        static_cast<bool>(processor.shutdown()) &&
        static_cast<bool>(processor.shutdown()),
        "The real processor must support ordered flush and idempotent shutdown");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " render foundation pipeline check(s) failed.\n";
        return 1;
    }
    std::cout << "Render foundation pipeline checks passed.\n";
    return 0;
}
