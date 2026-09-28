#include "workspace/editor_workspace.h"

#include "asset_file.h"

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
