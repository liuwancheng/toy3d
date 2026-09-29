#include "workspace/editor_workspace.h"

#include "asset_file.h"
#include "asset_tools/material_asset_tools.h"

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
    AssetFileIndex engine_index;
    engine_index.asset_id = engine_id;
    engine_index.root_type = "test.EngineMesh";
    engine_index.schema_version = 1;
    const std::vector<AssetSegmentData> segments = {{"type_data", 1, true, {1u}}};
    const auto engine_bytes = encode_asset_file(engine_index, segments);
    check(engine_bytes.succeeded(), "engine asset fixture must encode");
    if (!engine_bytes.succeeded()) return 1;
    check(platform.write_binary(PhysicalPath((fixture / "engine" / "asset" / "mesh.asset").u8string()),
                                engine_bytes.value(), FileWriteMode::CreateNew).succeeded(),
          "engine asset fixture must be written");
    AssetFileIndex project_index;
    project_index.asset_id = project_id;
    project_index.root_type = "test.ProjectScene";
    project_index.schema_version = 1;
    AssetRef engine_reference;
    engine_reference.asset_id = engine_id;
    engine_reference.expected_type = engine_index.root_type;
    engine_reference.strength = AssetRefStrength::Strong;
    project_index.dependencies.push_back(engine_reference);
    const auto project_bytes = encode_asset_file(project_index, segments);
    check(project_bytes.succeeded(), "project asset fixture must encode");
    if (!project_bytes.succeeded()) return 1;
    check(platform.write_binary(PhysicalPath((fixture / "project" / "asset" / "scene.asset").u8string()),
                                project_bytes.value(), FileWriteMode::CreateNew).succeeded(),
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
        check(workspace.files().write_binary(duplicate, engine_bytes.value(), FileWriteMode::CreateNew).succeeded(),
              "duplicate identity fixture must be written");
        check(!workspace.refresh() && workspace.catalog().entries.size() == 2 && !workspace.error().empty(),
              "duplicate identities across roots must reject refresh and preserve the previous catalog");
        check(workspace.files().remove_file(duplicate).succeeded() && workspace.refresh(),
              "workspace must recover after removing invalid input");
        check(!scan_asset_catalog(workspace.files(), std::vector<VirtualPath>{virtual_path("/Project"),
                 virtual_path("/Project/nested")}).succeeded(), "overlapping scan roots must be rejected");
        check(!scan_asset_catalog(workspace.files(), std::vector<VirtualPath>{}).succeeded(),
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
        check(create_material_instance_asset_in_workspace(workspace, "/Project/MI_Test.asset", instance, schema, instance_id).succeeded(),
            "instance must be created with a strong root Material reference");
        MaterialInstanceAssetData reopened_instance;
        check(read_material_instance_asset(workspace.types(), workspace.files(), virtual_path("/Project/MI_Test.asset"),
            reopened_instance, &workspace.catalog().index).succeeded() && reopened_instance.parent.asset_id == material_id,
            "instance parent must survive save and reload");
        auto invalid_instance = instance;
        invalid_instance.parent.asset_id = instance_id;
        check(!create_material_instance_asset_in_workspace(workspace, "/Project/MI_Nested.asset", invalid_instance, schema, rejected_id).succeeded() &&
            !workspace.files().stat(virtual_path("/Project/MI_Nested.asset")).succeeded(), "instance parent must not be another instance");
        auto invalid_material = material;
        invalid_material.overrides.push_back({"unknown", 1.0f});
        check(!create_material_asset_in_workspace(workspace, "/Project/M_Invalid.asset", invalid_material, schema, rejected_id).succeeded(),
            "new material must reject unknown parameters");
        check(!create_material_asset_in_workspace(workspace, "/Engine/M_Invalid.asset", material, schema, rejected_id).succeeded(),
            "creation API must enforce the read-only engine root");
        // Material saves must preserve optional blobs through the existing generic contract.
        const AssetSegmentData optional_blob{"thumbnail_png", 2, false, {1u, 2u, 3u}};
        if (original.succeeded())
        {
            const auto with_blob = replace_asset_segments(original.value(), {optional_blob});
            check(with_blob.succeeded(), "optional material blob fixture must encode");
            if (with_blob.succeeded())
                check(workspace.files().write_binary_atomic(material_path, with_blob.value(), FilePublishMode::Replace).succeeded(),
                    "optional material blob fixture must publish");
        }
        MaterialAssetData modified = reopened;
        modified.two_sided = false;
        const auto* location = workspace.catalog().index.find(material_id);
        check(location != nullptr, "created material must be indexed");
        if (location)
        {
            check(!save_asset(workspace.types(), SchemaMigrationRegistry{}, workspace.files(), material_path,
                location->index, modified, [](const MaterialAssetData& value) { return validate_material_asset(value); }).succeeded(),
                "material save must reject dropping optional blob bytes");
            check(save_asset(workspace.types(), SchemaMigrationRegistry{}, workspace.files(), material_path,
                location->index, modified, [](const MaterialAssetData& value) { return validate_material_asset(value); }, {optional_blob}).succeeded(),
                "existing generic save must support the generated Material DTO");
            check(read_material_asset(workspace.types(), workspace.files(), material_path, reopened).succeeded() && !reopened.two_sided,
                "updated Material value must reopen identically");
        }
        const auto instance_path = virtual_path("/Project/MI_Test.asset");
        const auto instance_bytes = workspace.files().read_binary(instance_path);
        const auto instance_index = inspect_asset(workspace.files(), instance_path);
        check(instance_bytes.succeeded() && instance_index.succeeded(), "instance index fixture must load");
        if (instance_bytes.succeeded() && instance_index.succeeded())
        {
            ValueWriter typed_writer;
            check(encode_value(typed_writer, instance).succeeded(), "instance typed fixture must encode");
            auto mismatched_index = instance_index.value();
            mismatched_index.dependencies.clear();
            const auto mismatch = encode_asset_file(mismatched_index, {{"type_data", 1, true, typed_writer.bytes()}});
            check(mismatch.succeeded(), "mismatched dependency fixture must encode");
            if (mismatch.succeeded())
            {
                check(workspace.files().write_binary_atomic(instance_path, mismatch.value(), FilePublishMode::Replace).succeeded(),
                    "dependency mismatch fixture must publish");
                MaterialInstanceAssetData retained = reopened_instance;
                check(!read_material_instance_asset(workspace.types(), workspace.files(), instance_path, retained,
                    &workspace.catalog().index).succeeded() && retained.parent.asset_id == material_id,
                    "dependency mismatch must reject without replacing output");
                check(workspace.files().write_binary_atomic(instance_path, instance_bytes.value(), FilePublishMode::Replace).succeeded(),
                    "valid instance fixture must restore");
            }
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
