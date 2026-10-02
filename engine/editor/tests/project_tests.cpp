#include "workspace/editor_project.h"

#include <filesystem>
#include <iostream>
#include "asset/scene/scene_asset.h"
#include "compiler/shader_source_discovery.h"
#include "workspace/editor_workspace.h"

namespace
{
    int failures = 0;
    void check(bool result, const std::string& message)
    {
        if (!result)
        {
            ++failures;
            std::cerr << "FAILED: " << message << '\n';
        }
    }
    toy3d::VirtualPath path(const std::string& value)
    {
        return toy3d::VirtualPath::parse(value).value();
    }
    bool write(toy3d::FileSystem& files, const std::string& name, const std::string& text)
    {
        return files
            .write_binary_atomic(path(name), std::vector<std::uint8_t>(text.begin(), text.end()),
                                 toy3d::FilePublishMode::Replace)
            .succeeded();
    }
} // namespace

int main()
{
    using namespace toy3d;
    AssetId id;
    if (!AssetId::try_generate(id))
    {
        return 1;
    }
    NativePlatformFile platform;
    const PhysicalPath editor_directory(TOY3D_PROJECT_TEST_DEPLOYMENT);
    const PhysicalPath root(std::string(TOY3D_PROJECT_TEST_ROOT) + "/" + id.hex());
    if (!platform.create_directories(root).succeeded())
    {
        return 1;
    }
    GameProject dto;
    dto.id = id;
    dto.name = "ResourceGame";
    dto.engine_association = "toy3d_dev";
    const auto encoded = encode_game_project(dto);
    check(encoded.succeeded(), "Resource project serializes through the existing YAML dependency");
    if (!encoded.succeeded())
    {
        return 1;
    }
    const auto parsed = parse_game_project(encoded.value());
    check(parsed.succeeded() && parsed.value().id == id && parsed.value().modules.empty(),
          "Stable identity survives YAML round trip");
    check(!parse_game_project(encoded.value() + "name: Duplicate\n").succeeded(),
          "Duplicate project fields are rejected");
    check(!parse_game_project(encoded.value() + "unknown: value\n").succeeded(),
          "Unknown descriptor fields cannot become implicit behavior");
    check(!parse_game_project(encoded.value() + "---\nname: another\n").succeeded(),
          "Multiple YAML documents cannot change project identity");
    std::string aliased = encoded.value();
    aliased.replace(aliased.find("name: ResourceGame"), std::string("name: ResourceGame").size(),
                    "name: &name ResourceGame");
    aliased.replace(aliased.find("engine_association: toy3d_dev"), std::string("engine_association: toy3d_dev").size(),
                    "engine_association: *name");
    check(!parse_game_project(aliased).succeeded(), "Aliases are rejected before schema interpretation");
    check(!valid_project_name("CON") && !valid_project_name("../escape") && !valid_project_name("9Game"),
          "Project names are portable directory and module names");
    check(!EditorProject::create(root, "InvalidEditor", PhysicalPath("relative/bin")).succeeded(),
          "Launcher installation paths cannot fall back to the working directory");
    const auto created = EditorProject::create(root, "GameA", editor_directory);
    check(created.succeeded(), "Project creation publishes a complete descriptor and config");
    if (!created.succeeded())
    {
        std::cerr << created.status().message << '\n';
        return 1;
    }
    check(!EditorProject::create(root, "GameA", editor_directory).succeeded(),
          "Existing projects cannot be overwritten by creation");
    check(platform.exists(PhysicalPath(root.utf8() + "/GameA/launch_editor.bat")).value() &&
              platform.exists(PhysicalPath(root.utf8() + "/GameA/launch_editor.sh")).value(),
          "Project creation publishes both launchers with its descriptor");
    EditorProject project(editor_directory);
    check(project.open(created.value()).succeeded() && project.active(),
          "Resource project opens without running build scripts");
    check(project.description().name == "GameA" && !(project.description().id == id),
          "Creation generates a fresh stable identity");
    check(project.files().stat(path("/Game/config/game_engine.ini")).succeeded() &&
              project.files().stat(path("/Game/shader/include")).succeeded(),
          "Optional content roots and include root exist before mounting");
    check(!project.open(created.value()).succeeded(), "Project association remains immutable for the host lifetime");
    const auto batch = project.files().read_text_utf8(path("/Game/launch_editor.bat"));
    const auto shell = project.files().read_text_utf8(path("/Game/launch_editor.sh"));
    const auto launch_info = project.files().read_text_utf8(path("/Game/saved/editor_launch.txt"));
    check(batch.succeeded() && shell.succeeded() && batch.value().find(editor_directory.utf8()) == std::string::npos &&
              shell.value().find(editor_directory.utf8()) == std::string::npos && launch_info.succeeded() &&
              launch_info.value() == "GameA.toy\n" + editor_directory.utf8() + "\n",
          "Local engine association is cached in Saved, not embedded in portable scripts");
    check(write(project.files(), "/Game/launch_editor.bat", "custom launcher\n") &&
              project.files().remove_file(path("/Game/launch_editor.sh")).succeeded(),
          "Custom and missing launcher fixtures prepared");
    const PhysicalPath alternate_editor(root.utf8() + "/Editor B/bin");
    EditorProject reopened(alternate_editor);
    check(reopened.open(created.value()).succeeded(), "Opening an existing project repairs a missing launcher");
    check(reopened.files().read_text_utf8(path("/Game/launch_editor.bat")).value() == "custom launcher\n" &&
              reopened.files().read_text_utf8(path("/Game/launch_editor.sh")).value() == shell.value() &&
              reopened.files().read_text_utf8(path("/Game/saved/editor_launch.txt")).value() ==
                  "GameA.toy\n" + alternate_editor.utf8() + "\n",
          "Custom scripts survive opening while the installation cache follows the current Editor");
    const auto other = EditorProject::create(root, "GameB", editor_directory);
    EditorProject second(editor_directory);
    check(other.succeeded() && second.open(other.value()).succeeded(), "An independent second project opens");
    check(!(project.saved() == second.saved()) && !(project.description().id == second.description().id),
          "Saved artifacts and project identity are isolated");
    check(write(project.files(), "/Game/asset/only-a.txt", "A") &&
              !second.files().stat(path("/Game/asset/only-a.txt")).succeeded(),
          "Project assets never leak into another project mount");

    const auto discover = [&]()
    {
        return shader::discover_shader_sources(project.files(), path("/Game/shader"));
    };
    const auto fixture = platform.read_text_utf8(PhysicalPath(TOY3D_PROJECT_TEST_SHADER));
    check(fixture.succeeded(), "Parser fixture is independent of the source-side project");
    if (!fixture.succeeded())
    {
        return 1;
    }
    check(write(project.files(), "/Game/shader/new.shader", fixture.value()),
          "External source addition fixture written");
    auto sources = discover();
    check(sources.succeeded() && sources.value().size() == 1u && sources.value()[0].name == "Project/Surface/Painted" &&
              sources.value()[0].error.empty() &&
              sources.value()[0].pass_names == std::vector<std::string>{"Forward", "ShadowDepth", "HitProxy"} &&
              sources.value()[0].usage == shader::ShaderUsage::Material &&
              sources.value()[0].vertex_factory_support == shader::local_vertex_factory_support,
          "A source is discovered by its declaration and Pass metadata without a registration list");
    std::string generic = fixture.value();
    generic.replace(generic.find("Project/Surface/Painted"), std::string("Project/Surface/Painted").size(),
                    "Shared/Effects/Painted");
    const auto shadow_name = generic.find("Pass \"ShadowDepth\"");
    if (shadow_name == std::string::npos)
    {
        check(false, "Fixture contains a ShadowDepth role with an independent Pass display name");
        return 1;
    }
    generic.replace(shadow_name, std::string("Pass \"ShadowDepth\"").size(), "Pass \"Depth\"");
    check(write(project.files(), "/Game/shader/new.shader", generic), "Generic multi-Pass source fixture written");
    sources = discover();
    check(sources.succeeded() && sources.value().size() == 1u && sources.value()[0].name == "Shared/Effects/Painted" &&
              sources.value()[0].pass_names == std::vector<std::string>{"Forward", "Depth", "HitProxy"} &&
              sources.value()[0].pass_roles == std::vector<shader::ShaderPassRole>{shader::ShaderPassRole::Forward,
                                                                                   shader::ShaderPassRole::ShadowDepth,
                                                                                   shader::ShaderPassRole::HitProxy} &&
              sources.value()[0].error.empty(),
          "Discovery accepts non-project names and all parsed Passes without Editor Material policy");
    check(write(project.files(), "/Game/shader/new.shader", fixture.value()), "Project Material declaration restored");
    check(project.files()
              .rename_no_replace(path("/Game/shader/new.shader"), path("/Game/shader/moved.shader"))
              .succeeded(),
          "External source move succeeds");
    sources = discover();
    check(sources.succeeded() && sources.value().size() == 1u &&
              sources.value()[0].path == path("/Game/shader/moved.shader"),
          "Moving a source preserves logical identity while updating its path");
    check(write(project.files(), "/Game/shader/duplicate.shader", fixture.value()),
          "Duplicate declaration fixture written");
    sources = discover();
    check(sources.succeeded() && sources.value().size() == 2u && !sources.value()[0].error.empty() &&
              !sources.value()[1].error.empty() && sources.value()[0].name_conflict && sources.value()[1].name_conflict,
          "All conflicting declarations are diagnosed; traversal order never chooses a winner");
    check(write(project.files(), "/Game/shader/bad.shader", "broken Shader syntax") &&
              project.files().remove_file(path("/Game/shader/duplicate.shader")).succeeded(),
          "Malformed source and external deletion fixtures written");
    sources = discover();
    check(sources.succeeded() && sources.value().size() == 2u && !sources.value()[0].error.empty() &&
              sources.value()[1].error.empty(),
          "A malformed source is retained as a diagnostic without blocking valid sources");
    check(project.files().remove_file(path("/Game/shader/moved.shader")).succeeded(), "Source removed externally");
    sources = discover();
    check(sources.succeeded() && sources.value().size() == 1u,
          "Deleted declarations disappear on the next discovery snapshot");

    EditorWorkspacePaths paths;
    paths.engine_assets = PhysicalPath(TOY3D_PROJECT_TEST_ENGINE_ASSETS);
    paths.editor_resources = PhysicalPath(TOY3D_PROJECT_TEST_EDITOR_RESOURCES);
    paths.deployment = PhysicalPath(TOY3D_PROJECT_TEST_DEPLOYMENT);
    paths.saved = PhysicalPath(root.utf8() + "/no-project-saved");
    EditorWorkspace no_project;
    check(no_project.initialize(paths) && !no_project.has_project(),
          "Editor workspace supports startup without a project");
    SceneAssetData scene;
    const auto loaded = read_scene_asset(no_project.types(), no_project.files(), path("/Engine/Scenes/Default.scene"),
                                         scene, &no_project.catalog().index);
    check(loaded.succeeded() && scene.actors.size() == 3u,
          "Engine default is a real validated Scene with built-in actors");
    if (!loaded.succeeded())
    {
        std::cerr << loaded.message << '\n';
    }
    check(!write(no_project.files(), "/Engine/Scenes/Default.scene", "forbidden") &&
              !no_project.files().stat(path("/Project/only-a.txt")).succeeded(),
          "Engine assets stay read-only and there is no implicit project mount");

    // C++17 filesystem moves only the unique fixture to verify descriptor-relative paths.
    const auto moved = platform.join_relative(root, "MovedGame");
    const auto renamed = platform.rename_no_replace(project.root(), moved.value());
    check(renamed.succeeded(), "Resource project is portable as a directory");
    EditorProject relocated(editor_directory);
    check(relocated.open(PhysicalPath(moved.value().utf8() + "/GameA.toy")).succeeded() &&
              relocated.description().id == project.description().id && relocated.root() == moved.value(),
          "Relocating a descriptor retains identity and derives new content roots");
    check(relocated.files().rename_no_replace(path("/Game/GameA.toy"), path("/Game/Renamed GameA.toy")).succeeded(),
          "Descriptor filename can change independently of the portable project name");
    EditorProject renamed_project(editor_directory);
    check(renamed_project.open(PhysicalPath(moved.value().utf8() + "/Renamed GameA.toy")).succeeded() &&
              renamed_project.files().read_text_utf8(path("/Game/saved/editor_launch.txt")).value() ==
                  "Renamed GameA.toy\n" + editor_directory.utf8() + "\n",
          "Launcher cache binds the actual descriptor after a move or rename");
    check(write(relocated.files(), "/Game/config/game_engine.ini", "[Window]\nWidth=bad\n"),
          "Malformed project configuration fixture written");
    EditorProject rejected(editor_directory);
    check(!rejected.open(renamed_project.descriptor()).succeeded() && !rejected.active(),
          "Invalid typed project configuration never publishes an active association");
    const auto removed = platform.remove_directory_tree(root);
    check(removed.succeeded(), "Owned isolated fixture is cleaned");
    return failures == 0 ? 0 : 1;
}
