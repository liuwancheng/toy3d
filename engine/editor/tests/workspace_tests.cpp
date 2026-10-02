#include "workspace/editor_workspace.h"

#include "asset/asset_file.h"
#include "asset/asset_pair.h"
#include "assets/material/material_asset_tools.h"
#include "asset/scene/scene_asset.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::VirtualPath virtual_path(const std::string& value)
    {
        const auto parsed = toy3d::VirtualPath::parse(value);
        check(parsed.succeeded(), "fixture virtual path must parse");
        return parsed.succeeded() ? parsed.value() : toy3d::VirtualPath{};
    }
} // namespace

int main()
{
    using namespace toy3d;
    // C++17 filesystem is limited to fixture path composition and final cleanup;
    // workspace and asset operations use the existing shared FileSystem.
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path fixture = fs::temp_directory_path() / ("toy3d_workspace_" + std::to_string(stamp));
    NativePlatformFile platform;
    EditorWorkspacePaths paths;
    paths.project_assets = PhysicalPath((fixture / "project" / "asset").u8string());
    paths.engine_assets = PhysicalPath((fixture / "engine" / "asset").u8string());
    paths.editor_resources = PhysicalPath((fixture / "editor" / "resources").u8string());
    paths.deployment = PhysicalPath((fixture / "bin").u8string());
    for (const PhysicalPath& directory : {paths.project_assets, paths.engine_assets,
                                         paths.editor_resources, paths.deployment})
        check(platform.create_directories(directory).succeeded(), "fixture directory must be created");

    AssetId engine_id;
    AssetId project_id;
    check(AssetId::parse("11111111111111111111111111111111", engine_id) &&
              AssetId::parse("22222222222222222222222222222222", project_id), "fixture identities must parse");
    if (!engine_id.valid() || !project_id.valid()) return 1;
    TypeRegistry fixture_types;
    check(register_material_asset_types(fixture_types).succeeded() && fixture_types.freeze().succeeded(),
        "fixture material types must register");
    MaterialAssetData engine_material;
    engine_material.shader_name = "Toy3d/Surface/Phong";
    const auto engine_bytes = encode_material_asset_pair(fixture_types, engine_id, engine_material);
    check(engine_bytes.succeeded(), "engine asset fixture must encode");
    if (!engine_bytes.succeeded()) return 1;
    check(platform.write_binary(PhysicalPath((fixture / "engine" / "asset" / "mesh.asset").u8string()),
                                engine_bytes.value().asset, FileWriteMode::CreateNew).succeeded(),
          "engine asset fixture must be written");
    AssetRef engine_reference;
    engine_reference.asset_id = engine_id;
    engine_reference.expected_type = "toy3d.MaterialAssetData";
    engine_reference.strength = AssetRefStrength::Strong;
    MaterialInstanceAssetData project_instance;
    project_instance.parent = engine_reference;
    const auto project_bytes = encode_material_instance_asset_pair(fixture_types, project_id,
        project_instance);
    check(project_bytes.succeeded(), "project asset fixture must encode");
    if (!project_bytes.succeeded()) return 1;
    check(platform.write_binary(PhysicalPath((fixture / "project" / "asset" / "scene.asset").u8string()),
                                project_bytes.value().asset, FileWriteMode::CreateNew).succeeded(),
          "project asset fixture must be written");

    {
        EditorWorkspace workspace;
        check(workspace.initialize(paths) && workspace.ready(), "workspace with distinct roots must initialize");
        check(workspace.catalog().entries.size() == 2 &&
                  workspace.catalog().index.resolve(engine_reference).succeeded(),
              "project strong references must resolve engine assets in the combined catalog");
        check(workspace.files().write_binary(virtual_path("/Engine/forbidden.txt"), {1u},
                                             FileWriteMode::CreateNew).code == FileErrorCode::AccessDenied,
              "engine source mount must be read only");
        check(workspace.files().write_binary(virtual_path("/Editor/Resources/forbidden.txt"), {1u},
                                             FileWriteMode::CreateNew).code == FileErrorCode::AccessDenied,
              "Editor interface source mount must be read only");
        const VirtualPath authored = virtual_path("/Project/authored.txt");
        check(workspace.files().write_binary(authored, {42u}, FileWriteMode::CreateNew).succeeded() &&
                  platform.read_binary(PhysicalPath((fixture / "project" / "asset" / "authored.txt").u8string())).succeeded() &&
                  !platform.stat(PhysicalPath((fixture / "bin" / "authored.txt").u8string())).succeeded(),
              "project writes must target source assets, never the deployed directory");
        const VirtualPath duplicate = virtual_path("/Project/duplicate.asset");
        check(workspace.files().write_binary(duplicate, engine_bytes.value().asset, FileWriteMode::CreateNew).succeeded(),
              "duplicate identity fixture must be written");
        check(!workspace.refresh() && workspace.catalog().entries.size() == 2 && !workspace.error().empty(),
              "duplicate identities across roots must reject refresh and preserve the previous catalog");
        check(workspace.files().remove_file(duplicate).succeeded() && workspace.refresh(),
              "workspace must recover after removing invalid input");
        AssetId scene_id;
        check(AssetId::parse("33333333333333333333333333333333", scene_id),
            "scene identity must parse");
        SceneAssetData scene;
        SceneActorData scene_actor;
        scene_actor.id = "44444444444444444444444444444444";
        scene_actor.root_component_id = "55555555555555555555555555555555";
        scene_actor.kind = "DirectionalLight";
        scene_actor.type = "toy3d.DirectionalLightActor";
        SceneComponentData root;
        root.id = scene_actor.root_component_id;
        root.type = "toy3d.DirectionalLightComponent";
        SceneDirectionalLightData light;
        light.shadow.receiver_bias = 0.4f;
        light.shadow.cascade_count = 3;
        light.shadow.distribution_exponent = 4.0f;
        light.shadow.map_resolution = 1024;
        root.properties = light;
        scene_actor.components.push_back(root);
        scene.actors.push_back(scene_actor);
        const auto scene_pair = encode_scene_asset_pair(workspace.types(), scene_id, scene,
            &workspace.catalog().index);
        const VirtualPath scene_path = virtual_path("/Project/scene.scene");
        check(scene_pair.succeeded() && workspace.asset_pairs().publish(scene_path,
            scene_pair.value(), FilePublishMode::CreateNew).succeeded() && workspace.refresh(),
            "Scene must publish and scan beside an ordinary .asset with the same stem");
        // C++17 get checks the known typed payload after the YAML roundtrip.
        SceneAssetData reopened_scene;
        check(read_scene_asset(workspace.types(), workspace.files(), scene_path, reopened_scene,
            &workspace.catalog().index).succeeded() && reopened_scene.actors.size() == 1u &&
            reopened_scene.actors[0].id == scene_actor.id &&
            std::get<SceneDirectionalLightData>(reopened_scene.actors[0].components[0].properties).shadow.receiver_bias == 0.4f &&
            std::get<SceneDirectionalLightData>(reopened_scene.actors[0].components[0].properties).shadow.cascade_count == 3 &&
            std::get<SceneDirectionalLightData>(reopened_scene.actors[0].components[0].properties).shadow.distribution_exponent == 4.0f &&
            std::get<SceneDirectionalLightData>(reopened_scene.actors[0].components[0].properties).shadow.map_resolution == 1024,
            "Scene Actor identity must survive .scene YAML roundtrip");
        AssetId legacy_id;
        check(AssetId::parse("88888888888888888888888888888888", legacy_id),
            "legacy scene identity must parse");
        const auto legacy_pair = encode_scene_asset_pair(workspace.types(), legacy_id, scene);
        check(legacy_pair.succeeded(), "legacy scene fixture must encode");
        if (legacy_pair.succeeded())
        {
            std::string legacy_text(legacy_pair.value().asset.begin(), legacy_pair.value().asset.end());
            const std::string current_version = "schema_version: 6";
            const std::size_t version_position = legacy_text.find(current_version);
            check(version_position != std::string::npos, "Scene schema version fixture must be present");
            if (version_position != std::string::npos)
            {
                legacy_text.replace(version_position, current_version.size(), "schema_version: 3");
                const VirtualPath legacy_path = virtual_path("/Project/legacy.scene");
                const std::vector<std::uint8_t> legacy_bytes(legacy_text.begin(), legacy_text.end());
                check(workspace.files().write_binary(legacy_path, legacy_bytes,
                    FileWriteMode::CreateNew).succeeded(), "legacy scene fixture must be written");
                SceneAssetData rejected_scene;
                check(!read_scene_asset(workspace.types(), workspace.files(), legacy_path,
                    rejected_scene).succeeded() && !workspace.refresh(),
                    "meter Scene must fail strict centimeter schema validation");
                check(workspace.files().remove_file(legacy_path).succeeded() && workspace.refresh(),
                    "workspace must recover after removing unsupported Scene schema");
            }
        }
        SceneActorData invalid_child = scene_actor;
        invalid_child.id = "66666666666666666666666666666666";
        invalid_child.root_component_id = "77777777777777777777777777777777";
        invalid_child.components[0].id = invalid_child.root_component_id;
        invalid_child.components[0].parent_component_id = scene_actor.root_component_id;
        scene.actors[0].components[0].parent_component_id = invalid_child.root_component_id;
        scene.actors.push_back(invalid_child);
        check(validate_scene_asset(scene).code == AssetErrorCode::Value,
            "Scene attachment cycle must be rejected before publishing");
        scene.actors.pop_back();
        scene.actors[0].components[0].parent_component_id.clear();
        check(!workspace.asset_pairs().publish(virtual_path("/Project/wrong.asset"),
            scene_pair.value(), FilePublishMode::CreateNew).succeeded() &&
            !workspace.asset_pairs().publish(virtual_path("/Project/wrong.scene"),
            engine_bytes.value(), FilePublishMode::CreateNew).succeeded(),
            "descriptor extension must match the reflected root type");
        const VirtualPath copied_scene_path = virtual_path("/Project/scene_copy.scene");
        const auto copied_scene = workspace.copy_asset(scene_id, copied_scene_path);
        check(copied_scene.succeeded() && copied_scene.value().valid() &&
            !(copied_scene.value() == scene_id), "Scene copy must allocate a new Asset ID");
        if (copied_scene.succeeded())
        {
            const VirtualPath moved_scene_path = virtual_path("/Project/scene_moved.scene");
            check(workspace.move_asset(copied_scene.value(), moved_scene_path).succeeded() &&
                workspace.catalog().index.find(copied_scene.value())->path == moved_scene_path,
                "Scene move must preserve identity");
            check(workspace.delete_asset(copied_scene.value()).succeeded(),
                "Scene delete must remove its catalog entry");
        }
        check(!scan_asset_catalog(workspace.types(), workspace.files(), std::vector<VirtualPath>{virtual_path("/Project"),
                 virtual_path("/Project/nested")}).succeeded(), "overlapping scan roots must be rejected");
        check(!scan_asset_catalog(workspace.types(), workspace.files(), std::vector<VirtualPath>{}).succeeded(),
              "empty scan roots must be rejected");

        shader::ShaderParameterSchema schema;
        schema.logical_layout_hash = shader::calculate_shader_parameter_logical_layout_hash(schema);
        schema.schema_identity = shader::calculate_shader_parameter_schema_identity(schema);
        MaterialAssetData material;
        material.shader_name = "Toy3d/Surface/Phong";
        material.two_sided = true;
        std::string destination;
        std::string creation_error;
        check(material_asset_destination("/Project", "M_Test", destination, creation_error) &&
            destination == "/Project/M_Test.asset", "material destination must use current project directory");
        check(!material_asset_destination("/Engine", "Forbidden", destination, creation_error) &&
            !material_asset_destination("/Project", "../escape", destination, creation_error),
            "read-only or escaping material destination must be rejected");
        AssetId material_id;
        check(create_material_asset_in_workspace(workspace, "/Project/M_Test.asset", material, schema, material_id).succeeded() &&
            material_id.valid(), "material creation must publish and refresh");
        const auto material_path = virtual_path("/Project/M_Test.asset");
        const auto original = workspace.files().read_binary(material_path);
        AssetId rejected_id;
        check(!create_material_asset_in_workspace(workspace, "/Project/M_Test.asset", material, schema, rejected_id).succeeded() &&
            !rejected_id.valid(), "same-name creation must reject without publishing an ID");
        const auto unchanged = workspace.files().read_binary(material_path);
        check(original.succeeded() && unchanged.succeeded() && original.value() == unchanged.value(),
            "same-name creation must preserve the existing asset bytes");
        MaterialAssetData reopened;
        check(read_material_asset(workspace.types(), workspace.files(), material_path, reopened, &workspace.catalog().index).succeeded() &&
            reopened.shader_name == material.shader_name && reopened.two_sided, "saved material must load without Shader source files");
        MaterialInstanceAssetData instance;
        instance.parent.asset_id = material_id;
        instance.parent.expected_type = "toy3d.MaterialAssetData";
        AssetId instance_id;
        const auto instance_created = create_material_instance_asset_in_workspace(workspace, "/Project/MI_Test.asset", instance, schema, instance_id);
        if (!instance_created.succeeded()) std::cerr << "Instance create: " << instance_created.message << '\n';
        check(instance_created.succeeded(),
            "instance must be created with a strong root Material reference");
        MaterialInstanceAssetData reopened_instance;
        check(read_material_instance_asset(workspace.types(), workspace.files(), virtual_path("/Project/MI_Test.asset"),
            reopened_instance, &workspace.catalog().index).succeeded() && reopened_instance.parent.asset_id == material_id,
            "instance parent must survive save and reload");
        auto invalid_instance = instance;
        invalid_instance.parent.asset_id = instance_id;
        check(!create_material_instance_asset_in_workspace(workspace, "/Project/MI_Nested.asset", invalid_instance, schema, rejected_id).succeeded() &&
            !workspace.files().stat(virtual_path("/Project/MI_Nested.asset")).succeeded(), "parent expected_type must match its actual instance DTO");
        invalid_instance.parent.expected_type = "toy3d.MaterialInstanceAssetData";
        AssetId nested_id;
        check(create_material_instance_asset_in_workspace(workspace, "/Project/MI_Nested.asset", invalid_instance, schema, nested_id).succeeded(),
            "nested instance creation must preserve a direct Instance Parent");
        AssetRef nested_ref;
        nested_ref.asset_id = nested_id;
        nested_ref.expected_type = "toy3d.MaterialInstanceAssetData";
        const auto hierarchy = read_material_hierarchy(workspace.types(), workspace.files(), workspace.catalog().index, nested_ref);
        check(hierarchy.succeeded() && hierarchy.value().layers.size() == 3u && hierarchy.value().layers.back().overrides.empty(),
            "three-layer root-to-leaf chain resolves without copying defaults into overrides");
        AssetRef deepest = nested_ref;
        // Direct encoding builds corrupt/over-limit input independently from
        // the creation UI, so the bounded reader is exercised on actual files.
        for (std::size_t depth = 3u; depth <= maximum_material_parent_depth; ++depth)
        {
            AssetId next_id;
            check(AssetId::try_generate(next_id), "allocate depth fixture");
            MaterialInstanceAssetData layer;
            layer.parent = deepest;
            const auto bytes = encode_material_instance_asset_pair(workspace.types(), next_id, layer);
            const auto path = virtual_path("/Project/MI_Depth" + std::to_string(depth + 1u) + ".asset");
            check(bytes.succeeded() && workspace.asset_pairs().publish(path, bytes.value(), FilePublishMode::CreateNew).succeeded(),
                "publish depth fixture");
            deepest = {next_id, {}, "toy3d.MaterialInstanceAssetData", AssetRefStrength::Strong};
            if (depth == maximum_material_parent_depth - 1u)
            {
                check(workspace.refresh(), "refresh exactly 64 layers");
                const auto limit = read_material_hierarchy(workspace.types(), workspace.files(), workspace.catalog().index, deepest);
                check(limit.succeeded() && limit.value().layers.size() == maximum_material_parent_depth, "exactly 64 layers must resolve");
            }
        }
        check(workspace.refresh() && !read_material_hierarchy(workspace.types(), workspace.files(), workspace.catalog().index, deepest).succeeded(),
            "65-layer input must fail without unbounded recursion");
        auto invalid_material = material;
        invalid_material.overrides.push_back({"unknown", 1.0f});
        check(!create_material_asset_in_workspace(workspace, "/Project/M_Invalid.asset", invalid_material, schema, rejected_id).succeeded(),
            "new material must reject unknown parameters");
        check(!create_material_asset_in_workspace(workspace, "/Engine/M_Invalid.asset", material, schema, rejected_id).succeeded(),
            "creation API must enforce the read-only engine root");
        AssetId obsolete_id;
        check(AssetId::try_generate(obsolete_id), "obsolete fixture ID");
        const auto obsolete_bytes = encode_material_asset(workspace.types(), obsolete_id, material);
        const auto obsolete_path = virtual_path("/Project/M_Obsolete.asset");
        check(obsolete_bytes.succeeded() && workspace.files().write_binary_atomic(obsolete_path,
            obsolete_bytes.value(), FilePublishMode::CreateNew).succeeded(), "obsolete fixture publication");
        check(!workspace.refresh() && workspace.catalog().index.find(material_id) != nullptr,
            "old binary asset must be rejected without losing the last catalog");
        check(workspace.files().remove_file(obsolete_path).succeeded() && workspace.refresh(),
            "catalog must recover after obsolete asset removal");
        const auto instance_path = virtual_path("/Project/MI_Test.asset");
        const auto instance_pair = workspace.asset_pairs().read(instance_path);
        check(instance_pair.succeeded(), "instance index fixture must load");
        if (instance_pair.succeeded())
        {
            ValueWriter typed_writer;
            check(encode_value(typed_writer, instance).succeeded(), "instance typed fixture must encode");
            auto mismatched_index = instance_pair.value().description.index;
            mismatched_index.dependencies.clear();
            const auto mismatch = encode_asset_pair(workspace.types(), mismatched_index, typed_writer.bytes(), {});
            check(mismatch.succeeded(), "mismatched dependency fixture must encode");
            if (mismatch.succeeded())
            {
                check(workspace.asset_pairs().publish(instance_path, mismatch.value(), FilePublishMode::Replace).succeeded(),
                    "dependency mismatch fixture must publish");
                MaterialInstanceAssetData retained = reopened_instance;
                check(!read_material_instance_asset(workspace.types(), workspace.files(), instance_path, retained,
                    &workspace.catalog().index).succeeded() && retained.parent.asset_id == material_id,
                    "dependency mismatch must reject without replacing output");
                const auto restored = encode_asset_pair(workspace.types(), instance_pair.value().description.index,
                    instance_pair.value().description.type_data, {});
                check(restored.succeeded() && workspace.asset_pairs().publish(instance_path,
                    restored.value(), FilePublishMode::Replace).succeeded(),
                    "valid instance fixture must restore");
            }
        }
        check(workspace.delete_asset(material_id).code == AssetErrorCode::Conflict,
            "strong material references must block deletion");
        const auto copied_material_path = virtual_path("/Project/M_Test_Copy.asset");
        const auto copied_material = workspace.copy_asset(material_id, copied_material_path);
        check(copied_material.succeeded() && !(copied_material.value() == material_id) &&
            workspace.catalog().index.find(copied_material.value()) != nullptr &&
            !workspace.files().stat(virtual_path("/Project/M_Test_Copy.meta")).succeeded(),
            "copy of a descriptive material must assign a new ID without meta");
        if (copied_material.succeeded())
        {
            const auto moved_material_path = virtual_path("/Project/M_Test_Moved.asset");
            check(workspace.move_asset(copied_material.value(), moved_material_path).succeeded() &&
                workspace.catalog().index.find(copied_material.value())->path == moved_material_path &&
                !workspace.files().stat(copied_material_path).succeeded(),
                "moving a material must retain its ID and remove the old descriptor");
            check(workspace.delete_asset(copied_material.value()).succeeded() &&
                !workspace.files().stat(moved_material_path).succeeded(),
                "deleting a descriptive asset must remove it from the catalog");
        }
    }

    const PhysicalPath deployment_child((fixture / "bin" / "nested").u8string());
    check(platform.create_directories(deployment_child).succeeded(), "nested deployed fixture must be created");
    for (const PhysicalPath& invalid_root : {paths.deployment, deployment_child, PhysicalPath(fixture.u8string()),
                                           paths.engine_assets, paths.editor_resources})
    {
        EditorWorkspacePaths invalid_paths = paths;
        invalid_paths.project_assets = invalid_root;
        EditorWorkspace workspace;
        check(!workspace.initialize(invalid_paths) && !workspace.ready(),
              "project roots equal to, inside or containing protected roots must be rejected");
    }
    {
        EditorWorkspacePaths invalid_paths = paths;
        invalid_paths.project_assets = PhysicalPath((fixture / "missing").u8string());
        EditorWorkspace workspace;
        check(!workspace.initialize(invalid_paths), "missing project source must be rejected");
    }

    std::error_code error;
    fs::remove_all(fixture, error);
    check(!error, "workspace fixture cleanup must succeed");
    return failure_count == 0 ? 0 : 1;
}
