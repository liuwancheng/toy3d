#include "editor.h"

#include <cstdio>
#include "platform/platform_defines.h"
#include "asset/game_project.h"
#include "file_system/native_platform_file.h"
#include "file_system/directory_file_store.h"
#include "config/command_line_parser.h"
#include "imgui.h"
#include "logging/logger.h"
#include "gamescene/world/world.h"
#include "platform/model_file_picker.h"
#include "workspace/editor_workspace.h"
#include "threading/task_graph/graph_task.h"
#include "game_module_build.h"

namespace toy3d
{
    bool EditorApplication::scene_writable() const
    {
        return workspace_.has_project() && scene_session_.path().utf8().compare(0u, 9u, "/Project/") == 0;
    }

    void EditorApplication::request_scene_save()
    {
        if (play_session_.active())
        {
            return;
        }
        if (!workspace_.has_project())
        {
            save_scene_to_project_ = true;
            show_new_project_ = true;
            project_error_.clear();
            return;
        }
        if (scene_writable())
        {
            save_scene(scene_session_.path(), false);
        }
        else
        {
            if (asset_folder_.compare(0u, 8u, "/Project") != 0)
            {
                asset_folder_ = "/Project";
            }
            show_scene_save_as_ = true;
        }
    }

    void EditorApplication::load_startup_scene()
    {
        startup_pending_ = false;
        if (scene_session_.open_path(startup_scene_))
        {
            TOY_LOG_INFO("Opened startup Scene [{}].", startup_scene_);
            return;
        }
        const std::string reason = scene_session_.error();
        TOY_LOG_ERROR("Startup Scene [{}]: {}", startup_scene_, reason);
        if (startup_scene_ != "/Engine/Scenes/Default.scene" &&
            scene_session_.open_path("/Engine/Scenes/Default.scene"))
        {
            TOY_LOG_INFO("Opened engine default Scene [/Engine/Scenes/Default.scene].");
            notifications_.success("Default Scene",
                                   "Startup scene failed; the engine default scene is open. See Console.");
        }
        else
        {
            scene_error_ = "Engine default Scene could not load: " + scene_session_.error();
            TOY_LOG_ERROR("{}", scene_error_);
        }
    }

    void EditorApplication::request_project_open(const PhysicalPath& descriptor)
    {
        NativePlatformFile platform;
        const auto canonical = platform.canonical(descriptor);
        const auto parent = canonical.succeeded() ? platform.parent_path(canonical.value())
                                                  : FileResult<PhysicalPath>(canonical.status());
        if (!parent.succeeded())
        {
            project_error_ = parent.status().message;
            TOY_LOG_ERROR("Open Project: {}", project_error_);
            return;
        }
        DirectoryFileStoreDesc store_desc;
        store_desc.physical_root = parent.value();
        store_desc.writable = false;
        const auto store = DirectoryFileStore::create(platform, store_desc);
        if (!store.succeeded())
        {
            project_error_ = store.status().message;
            TOY_LOG_ERROR("Open Project: {}", project_error_);
            return;
        }
        FileSystem files;
        FileMountDesc mount;
        mount.virtual_root = VirtualPath::parse("/Game").value();
        mount.store = store.value();
        mount.access = MountAccess::ReadOnly;
        auto mounted = files.add_mount(mount);
        if (mounted.succeeded())
        {
            mounted = files.freeze();
        }
        if (!mounted.succeeded())
        {
            project_error_ = mounted.message;
            TOY_LOG_ERROR("Open Project: {}", project_error_);
            return;
        }
        const auto filename = canonical.value().utf8().substr(canonical.value().utf8().find_last_of("/\\") + 1u);
        const auto description = read_game_project(files, VirtualPath::parse("/Game/" + filename).value());
        if (!description.succeeded())
        {
            project_error_ = description.status().message;
            TOY_LOG_ERROR("Open Project: {}", project_error_);
            return;
        }
        EditorProject candidate{PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT)};
        const auto opened = candidate.open(descriptor);
        if (!opened.succeeded())
        {
            project_error_ = opened.message;
            TOY_LOG_ERROR("Open Project: {}", project_error_);
            return;
        }
        pending_project_ = candidate.descriptor();
        pending_editor_executable_ = PhysicalPath(std::string(TOY3D_EDITOR_DEPLOY_ROOT) + "/" + "Toy3dEditor"
#if WITH_WIN
                                                  + ".exe"
#elif WITH_MAC
                                                  + ".app/Contents/MacOS/" + "Toy3dEditor"
#endif
        );
        if (package_task_ || shaders_.busy() || model_import_.active() || skeletal_import_.active() ||
            texture_import_.active())
        {
            pending_project_ = {};
            project_scene_saved_ = false;
            project_error_ = "Finish or cancel the background operation before opening another project.";
            return;
        }
        if (!mesh_editor_.request_exit() || !material_editor_.request_exit())
        {
            waiting_material_project_ = true;
            return;
        }
        request_scene_action(SceneAction::SwitchProject);
    }

    void EditorApplication::launch_project()
    {
        if (pending_project_.empty())
        {
            return;
        }
        auto arguments = CommandLineParser::get_instance().launch_arguments();
        arguments.push_back("--Project=" + pending_project_.utf8());
        const auto launched = processes_.launch_detached(pending_editor_executable_, arguments);
        project_scene_saved_ = false;
        if (!launched.succeeded())
        {
            project_error_ = launched.message;
            TOY_LOG_ERROR("Launch Project Editor: {}", project_error_);
            return;
        }
        discard_scene_on_exit_ = true;
        window().close();
    }

    void EditorApplication::standalone_play()
    {
        if (!project_ || !project_->active() || game_executable_.empty() || !can_start_play())
        {
            return;
        }
        SceneAssetData data;
        AssetId id;
        if (!scene_session_.capture(data) || !AssetId::try_generate(id))
        {
            TOY_LOG_ERROR("Play Scene capture failed: {}", scene_session_.error());
            return;
        }
        const auto directory = VirtualPath::parse("/Saved/play").value();
        const auto created = workspace_.files().create_directories(directory);
        const auto path = VirtualPath::parse("/Saved/play/" + id.hex() + ".scene").value();
        const auto bytes = encode_scene_asset_pair(workspace_.types(), id, data, &workspace_.catalog().index);
        const auto saved =
            !created.succeeded() ? AssetStatus{AssetErrorCode::Io, {}, path.utf8(), {}, {}, created.message, created}
            : bytes.succeeded()  ? workspace_.asset_pairs().publish(path, bytes.value(), FilePublishMode::CreateNew)
                                 : bytes.status();
        if (!saved.succeeded())
        {
            TOY_LOG_ERROR("Play Scene snapshot: {}", saved.message);
            return;
        }
        auto arguments = CommandLineParser::get_instance().launch_arguments();
        arguments.push_back("--Project=" + project_->descriptor().utf8());
        arguments.push_back("--PlayScene=" + path.utf8());
        if (project_->description().modules.empty())
        {
            arguments.push_back("--EditorShaderArtifacts");
        }
        const auto launched = processes_.launch_detached(game_executable_, arguments);
        if (!launched.succeeded())
        {
            TOY_LOG_ERROR("Play Scene launch: {}. Build the project Game host first.", launched.message);
            return;
        }
        notifications_.success("Standalone Play",
                               "Game window launched. Runtime diagnostics are in the project Saved logs.");
    }

    void EditorApplication::open_project_settings()
    {
        if (!project_ || !project_->active())
        {
            return;
        }
        project_error_.clear();
        const auto path = VirtualPath::parse("/Game/config/game_engine.ini").value();
        const auto text = project_->files().read_text_utf8(path, 256u * 1024u);
        project_config_existed_ = text.succeeded();
        if (!text.succeeded() && text.status().code != FileErrorCode::NotFound)
        {
            project_error_ = text.status().message;
            return;
        }
        project_config_bytes_ = text.succeeded() ? text.value() : "";
        const auto parsed = ConsoleManager::parse_config(project_config_bytes_, path.utf8());
        if (!parsed.succeeded())
        {
            project_error_ = parsed.status().message;
            return;
        }
        project_overrides_ = parsed.value();
        const auto editor = project_overrides_.find("Editor.StartupScene");
        const auto game = project_overrides_.find("Game.StartupScene");
        std::snprintf(editor_startup_, sizeof(editor_startup_), "%s",
                      editor == project_overrides_.end() ? "" : editor->second.value.c_str());
        std::snprintf(game_startup_, sizeof(game_startup_), "%s",
                      game == project_overrides_.end() ? "" : game->second.value.c_str());
        if ((editor != project_overrides_.end() && editor->second.value.size() >= sizeof(editor_startup_)) ||
            (game != project_overrides_.end() && game->second.value.size() >= sizeof(game_startup_)))
        {
            project_error_ = "Startup Scene exceeds the settings editor path limit. Edit game_engine.ini directly.";
            return;
        }
        show_project_settings_ = true;
    }

    bool EditorApplication::save_project_settings()
    {
        auto values = project_overrides_;
        for (const auto& item : {std::pair<const char*, const char*>{"Editor.StartupScene", editor_startup_},
                                 std::pair<const char*, const char*>{"Game.StartupScene", game_startup_}})
        {
            const std::string value = item.second;
            if (!value.empty())
            {
                const auto path = VirtualPath::parse(value);
                if (!path.succeeded() || path.value().utf8() != value || value.size() < 6u ||
                    value.compare(value.size() - 6u, 6u, ".scene") != 0 ||
                    (value.compare(0u, 9u, "/Project/") != 0 && value.compare(0u, 8u, "/Engine/") != 0))
                {
                    project_error_ = "Use a .scene virtual path under /Project or /Engine, or leave it empty.";
                    return false;
                }
            }
            // Empty explicitly selects the default scene; removing the INI key
            // instead inherits the value from the base configuration.
            values[item.first] = {value, "game_engine.ini", 0u};
        }
        const auto encoded = ConsoleManager::encode_config(values);
        if (!encoded.succeeded())
        {
            project_error_ = encoded.status().message;
            return false;
        }
        const auto path = VirtualPath::parse("/Game/config/game_engine.ini").value();
        const auto latest = project_->files().read_text_utf8(path, 256u * 1024u);
        if ((project_config_existed_ && (!latest.succeeded() || latest.value() != project_config_bytes_)) ||
            (!project_config_existed_ && (latest.succeeded() || latest.status().code != FileErrorCode::NotFound)))
        {
            project_error_ = "game_engine.ini changed externally. Close and reopen Project Settings.";
            return false;
        }
        const auto saved = project_->files().write_binary_atomic(
            path, std::vector<std::uint8_t>(encoded.value().begin(), encoded.value().end()),
            project_config_existed_ ? FilePublishMode::Replace : FilePublishMode::CreateNew);
        if (!saved.succeeded())
        {
            project_error_ = saved.message;
            return false;
        }
        project_config_bytes_ = encoded.value();
        project_config_existed_ = true;
        project_overrides_ = std::move(values);
        notifications_.success("Project Settings", "Saved game_engine.ini. Startup settings apply next launch.");
        return true;
    }

    void EditorApplication::draw_project_dialogs()
    {
        if (package_operation_)
        {
            ImGui::Begin("Packaging Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextUnformatted("Building, cooking and packaging...");
            ImGui::TextWrapped("%s", package_operation_->output.utf8().c_str());
            if (ImGui::Button("Cancel") && package_operation_)
            {
                package_operation_->cancel.store(true);
            }
            ImGui::End();
        }
        if (show_new_project_)
        {
            ImGui::OpenPopup("New Project");
        }
        if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Resource project (.toy / YAML)");
            ImGui::SetNextItemWidth(460);
            ImGui::InputText("Parent folder", project_parent_, sizeof(project_parent_));
            ImGui::InputText("Name", project_name_, sizeof(project_name_));
            if (ImGui::Button("Browse..."))
            {
                std::string folder, error;
                if (!pick_project_folder(window(), folder, error))
                {
                    project_error_ = error;
                }
                else if (folder.size() >= sizeof(project_parent_))
                {
                    project_error_ = "Parent folder path is too long for this dialog.";
                }
                else if (!folder.empty())
                {
                    std::snprintf(project_parent_, sizeof(project_parent_), "%s", folder.c_str());
                }
                if (!project_error_.empty())
                {
                    TOY_LOG_ERROR("Select project parent: {}", project_error_);
                }
            }
            if (save_scene_to_project_)
            {
                ImGui::TextUnformatted("The current Scene will be saved in the new project.");
            }
            if (ImGui::Button(save_scene_to_project_ ? "Create and Save Scene" : "Create Project"))
            {
                const auto created = EditorProject::create(PhysicalPath(project_parent_), project_name_,
                                                           PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT));
                if (!created.succeeded())
                {
                    project_error_ = created.status().message;
                    TOY_LOG_ERROR("Create Project: {}", project_error_);
                }
                else
                {
                    project_error_.clear();
                    bool ready = true;
                    if (save_scene_to_project_)
                    {
                        EditorProject target{PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT)};
                        const auto opened = target.open(created.value());
                        SceneAssetData data;
                        AssetId id;
                        if (!opened.succeeded() || !scene_session_.capture(data) || !AssetId::try_generate(id))
                        {
                            project_error_ = "Project created, but Scene capture failed. " + scene_session_.error();
                            ready = false;
                        }
                        else
                        {
                            const auto bytes =
                                encode_scene_asset_pair(workspace_.types(), id, data, &workspace_.catalog().index);
                            AssetPairStore store(workspace_.types(), target.files());
                            const auto saved =
                                bytes.succeeded()
                                    ? store.publish(VirtualPath::parse("/Game/asset/Startup.scene").value(),
                                                    bytes.value(), FilePublishMode::CreateNew)
                                    : bytes.status();
                            const std::string config = "[Editor]\nStartupScene=/Project/"
                                                       "Startup.scene\n\n[Game]\nStartupScene=/Project/Startup.scene\n";
                            const auto configured =
                                saved.succeeded() ? target.files().write_binary_atomic(
                                                        VirtualPath::parse("/Game/config/game_engine.ini").value(),
                                                        std::vector<std::uint8_t>(config.begin(), config.end()),
                                                        FilePublishMode::Replace)
                                                  : FileStatus{};
                            if (!saved.succeeded() || !configured.succeeded())
                            {
                                project_error_ =
                                    "Project created; Scene/config save failed: " + saved.message + configured.message;
                                ready = false;
                            }
                            else
                            {
                                project_scene_saved_ = true;
                                project_scene_revision_ = world().content_revision();
                            }
                        }
                    }
                    if (!ready)
                    {
                        TOY_LOG_ERROR("Create Project and save Scene: {}", project_error_);
                    }
                    if (ready)
                    {
                        show_new_project_ = false;
                        save_scene_to_project_ = false;
                        ImGui::CloseCurrentPopup();
                        request_project_open(created.value());
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                show_new_project_ = false;
                save_scene_to_project_ = false;
                ImGui::CloseCurrentPopup();
            }
            if (!project_error_.empty())
            {
                ImGui::TextWrapped("%s", project_error_.c_str());
            }
            ImGui::EndPopup();
        }
        if (show_project_settings_)
        {
            ImGui::OpenPopup("Project Settings");
        }
        if (ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Project: %s", project_->description().name.c_str());
            ImGui::SetNextItemWidth(480);
            ImGui::InputText("Editor startup Scene", editor_startup_, sizeof(editor_startup_));
            ImGui::SetNextItemWidth(480);
            ImGui::InputText("Game startup Scene", game_startup_, sizeof(game_startup_));
            ImGui::TextDisabled("Empty uses /Engine/Scenes/Default.scene. Settings apply next launch.");
            if (ImGui::Button("Use Current Scene") && !scene_session_.path().empty())
            {
                if (scene_session_.path().utf8().size() >= sizeof(editor_startup_))
                {
                    project_error_ = "Scene path exceeds the settings editor limit.";
                }
                else
                {
                    std::snprintf(editor_startup_, sizeof(editor_startup_), "%s", scene_session_.path().utf8().c_str());
                }
            }
            if (ImGui::Button("Save Settings"))
            {
                if (save_project_settings())
                {
                    show_project_settings_ = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    TOY_LOG_ERROR("Save Project Settings: {}", project_error_);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                show_project_settings_ = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::TreeNode("Effective configuration (current session)"))
            {
                for (const auto& value : ConsoleManager::get_instance().snapshot())
                {
                    ImGui::TextWrapped("%s = %s  [%s:%zu]", value.first.c_str(), value.second.value.c_str(),
                                       value.second.source.c_str(), value.second.line);
                }
                ImGui::TreePop();
            }
            if (!project_error_.empty())
            {
                ImGui::TextWrapped("%s", project_error_.c_str());
            }
            ImGui::EndPopup();
        }
        if (!project_error_.empty() && !show_project_settings_ && !show_new_project_)
        {
            if (ImGui::Begin("Project Error"))
            {
                ImGui::TextWrapped("%s", project_error_.c_str());
                if (ImGui::Button("Dismiss"))
                {
                    project_error_.clear();
                }
            }
            ImGui::End();
        }
    }

    void EditorApplication::package_project()
    {
#if WITH_WIN
        if (!project_ || !project_->active() || package_task_ || play_session_.active() || startup_pending_ ||
            shaders_.busy() || model_import_.active() || skeletal_import_.active() || texture_import_.active() ||
            material_editor_.modal_pending())
        {
            return;
        }
        if (scene_dirty() || (material_editor_.edit_session().active() && material_editor_.edit_session().dirty()))
        {
            TOY_LOG_ERROR("Save Scene and Material changes before packaging.");
            return;
        }
        AssetId operation_id;
        if (!AssetId::try_generate(operation_id) || !TaskGraphInterface::is_running())
        {
            TOY_LOG_ERROR("Could not start project packaging.");
            return;
        }
        auto operation = std::make_shared<PackageOperation>();
        operation->output = PhysicalPath(project_->saved().utf8() + "/packages/" + project_->description().name + "-" +
                                         operation_id.hex().substr(0u, 8u));
        const std::vector<std::string> arguments{"-DTOY3D_PROJECT=" + project_->descriptor().utf8(),
                                                 std::string("-DTOY3D_BUILD=") + TOY3D_PACKAGE_BUILD,
                                                 "-DTOY3D_OUTPUT=" + operation->output.utf8(),
                                                 std::string("-DTOY3D_CONFIGURATION=") + TOY3D_MODULE_CONFIG,
                                                 "-DTOY3D_BUILD_EDITOR=OFF",
                                                 "-P",
                                                 TOY3D_PACKAGE_SCRIPT};
        try
        {
            package_task_ = dispatch_graph_task(TaskGraphInterface::get(), "Package project",
                                                [operation, arguments](NamedThread, const GraphEventRef&)
                                                {
                                                    NativeProcessService processes;
                                                    ProcessRunOptions options;
                                                    options.timeout_ms = 1200000u;
                                                    options.maximum_output_bytes = 16u * 1024u * 1024u;
                                                    options.cancel = &operation->cancel;
                                                    operation->result = processes.run(PhysicalPath(TOY3D_PACKAGE_CMAKE),
                                                                                      arguments, options);
                                                });
            package_operation_ = std::move(operation);
        }
        catch (const std::exception& error)
        {
            TOY_LOG_ERROR("Package project: {}", error.what());
        }
#else
        TOY_LOG_ERROR("Project packaging currently supports Windows only.");
#endif
    }

    void EditorApplication::tick_package()
    {
        if (!package_task_ || !package_task_->is_complete())
        {
            return;
        }
        const auto result = std::move(package_operation_->result);
        const bool succeeded = package_task_->get_outcome() == TaskOutcome::Succeeded && result.succeeded();
        if (succeeded)
        {
            last_package_output_ = package_operation_->output;
            notifications_.success("Package Project", "Game package is ready: " + last_package_output_.utf8());
            TOY_LOG_INFO("Project package: {}", last_package_output_.utf8());
        }
        else
        {
            TOY_LOG_ERROR("Package project failed: {}\n{}", result.message, result.output);
        }
        package_task_.reset();
        package_operation_.reset();
    }

    void EditorApplication::stop_package()
    {
        if (package_task_)
        {
            package_operation_->cancel.store(true);
            const auto waited =
                TaskGraphInterface::get().wait_until_task_completes(package_task_, NamedThread::GameThread);
            if (!waited.succeeded())
            {
                TOY_LOG_ERROR("Could not drain project packaging during shutdown.");
            }
            package_task_.reset();
            package_operation_.reset();
        }
    }
} // namespace toy3d
