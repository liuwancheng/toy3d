#include "editor.h"

#include <exception>
#include <algorithm>
#include <cctype>

#include "imgui.h"
#include "imgui_internal.h"

#include "gamescene/actor/actor.h"
#include "asset/asset_descriptor_path.h"
#include "gamescene/world/world.h"
#include "gamescene/scene_view.h"
#include "logging/logger.h"
#include "math/length_units.h"
#include "math/quaternion.h"
#include "panels/place_actors_panel.h"
#include "panels/scene_panels.h"
#include "panels/content_browser_panel.h"
#include "rendercore/frame_synchronization.h"
#include "workspace/editor_workspace.h"
#include "scene/placement/asset_placement.h"
#include "config/command_line_parser.h"

namespace toy3d
{
    bool EditorApplication::scene_dirty() const
    {
        return scene_session_.dirty();
    }

    bool EditorApplication::save_scene(const VirtualPath& path, bool create_new)
    {
        if (play_session_.active())
        {
            return false;
        }
        const bool saved = scene_session_.save(path, create_new);
        scene_error_ = scene_session_.error();
        if (!saved)
        {
            TOY_LOG_ERROR("Save Scene [{}]: {}", path.utf8(), scene_error_);
        }
        return saved;
    }

    bool EditorApplication::open_scene(const AssetId& id)
    {
        if (play_session_.active())
        {
            return false;
        }
        const bool opened = scene_session_.open(id);
        scene_error_ = scene_session_.error();
        if (!opened)
        {
            TOY_LOG_ERROR("Open Scene [{}]: {}", id.hex(), scene_error_);
        }
        return opened;
    }

    void EditorApplication::new_scene()
    {
        if (play_session_.active())
        {
            return;
        }
        if (!scene_session_.new_scene())
        {
            TOY_LOG_ERROR("New Scene failed: {}", scene_session_.error());
        }
        scene_error_ = scene_session_.error();
    }

    void EditorApplication::request_scene_action(SceneAction action, const AssetId& id)
    {
        if (play_session_.active())
        {
            if (action == SceneAction::Exit)
            {
                window().close();
            }
            return;
        }
        pending_scene_action_ = action;
        pending_scene_id_ = id;
        if (scene_dirty() && !(action == SceneAction::SwitchProject && project_scene_saved_ &&
                               project_scene_revision_ == world().content_revision()))
        {
            scene_confirm_requested_ = true;
        }
        else
        {
            pending_scene_action_ = SceneAction::None;
            if (action == SceneAction::New)
            {
                new_scene();
            }
            else if (action == SceneAction::Open)
            {
                open_scene(id);
            }
            else if (action == SceneAction::Exit)
            {
                window().close();
            }
            else if (action == SceneAction::SwitchProject)
            {
                launch_project();
            }
        }
    }

    bool EditorApplication::on_close_requested()
    {
        if (play_session_.active())
        {
            stop_play();
        }
        if (!material_editor_.request_exit())
        {
            return false;
        }
        if (discard_scene_on_exit_)
        {
            return true;
        }
        if (!scene_dirty())
        {
            return true;
        }
        pending_scene_action_ = SceneAction::Exit;
        scene_confirm_requested_ = true;
        return false;
    }

    void EditorApplication::draw_scene_dialogs()
    {
        if (scene_confirm_requested_)
        {
            ImGui::OpenPopup("Unsaved Scene");
            scene_confirm_requested_ = false;
        }
        if (ImGui::BeginPopupModal("Unsaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("The current Scene has unsaved changes.");
            if (ImGui::Button("Save"))
            {
                if (!scene_writable())
                {
                    request_scene_save();
                    if (show_scene_save_as_ || show_new_project_)
                    {
                        ImGui::CloseCurrentPopup();
                    }
                }
                else if (save_scene(scene_session_.path(), false))
                {
                    const SceneAction action = pending_scene_action_;
                    const AssetId id = pending_scene_id_;
                    pending_scene_action_ = SceneAction::None;
                    ImGui::CloseCurrentPopup();
                    request_scene_action(action, id);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                const SceneAction action = pending_scene_action_;
                const AssetId id = pending_scene_id_;
                pending_scene_action_ = SceneAction::None;
                ImGui::CloseCurrentPopup();
                if (action == SceneAction::New)
                {
                    new_scene();
                }
                else if (action == SceneAction::Open)
                {
                    open_scene(id);
                }
                else if (action == SceneAction::Exit)
                {
                    discard_scene_on_exit_ = true;
                    window().close();
                }
                else if (action == SceneAction::SwitchProject)
                {
                    launch_project();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pending_scene_action_ = SceneAction::None;
                ImGui::CloseCurrentPopup();
            }
            if (!scene_error_.empty())
            {
                ImGui::TextWrapped("%s", scene_error_.c_str());
            }
            ImGui::EndPopup();
        }
        if (show_scene_save_as_)
        {
            ImGui::OpenPopup("Save Scene As");
        }
        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Folder: %s", asset_folder_.c_str());
            ImGui::InputText("Scene name", scene_name_, sizeof(scene_name_));
            if (ImGui::Button("Save Scene"))
            {
                const std::string name = scene_name_;
                const bool valid_name = !name.empty() && std::all_of(name.begin(), name.end(),
                                                                     [](unsigned char c)
                                                                     {
                                                                         return std::isalnum(c) || c == '_' || c == '-';
                                                                     });
                if (!valid_name)
                {
                    scene_error_ = "Use letters, numbers, underscore or dash for the Scene name.";
                }
                else
                {
                    const auto path = VirtualPath::parse(asset_folder_ + "/" + name + ".scene");
                    if (!path.succeeded())
                    {
                        scene_error_ = path.status().message;
                    }
                    else if (save_scene(path.value(), true))
                    {
                        show_scene_save_as_ = false;
                        ImGui::CloseCurrentPopup();
                        if (pending_scene_action_ != SceneAction::None)
                        {
                            const SceneAction action = pending_scene_action_;
                            const AssetId id = pending_scene_id_;
                            pending_scene_action_ = SceneAction::None;
                            request_scene_action(action, id);
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                show_scene_save_as_ = false;
                pending_scene_action_ = SceneAction::None;
                ImGui::CloseCurrentPopup();
            }
            if (!scene_error_.empty())
            {
                ImGui::TextWrapped("%s", scene_error_.c_str());
            }
            ImGui::EndPopup();
        }
        if (!scene_error_.empty() && !show_scene_save_as_ && pending_scene_action_ == SceneAction::None)
        {
            if (ImGui::Begin("Scene Error"))
            {
                ImGui::TextWrapped("%s", scene_error_.c_str());
                if (ImGui::Button("Dismiss"))
                {
                    scene_error_.clear();
                }
            }
            ImGui::End();
        }
    }
    bool EditorApplication::on_initialize()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return false;
        }
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.TabRounding = 3.0f;
        layout_path_ = saved_root_ + "/editor_layout.ini";
        ImGui::GetIO().IniFilename = layout_path_.c_str();
        if (!workspace_.has_project())
        {
            asset_folder_ = "/Engine";
            show_engine_content_ = true;
        }
        // Scene-image gestures edit content; dock panels move by their title/tab.
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
        if (!window().enable_file_drop(true))
        {
            TOY_LOG_WARN("External asset file drop is unavailable on this platform.");
        }
        if (!actor_factory_.initialize())
        {
            return false;
        }
        scene_session_.bind(world());
        MaterialTextureValues textures;
        const auto defaults = actor_factory_.default_material()->material();
        for (const auto& resource : defaults->parameter_schema().resources)
        {
            const auto found = defaults->desc().texture_defaults.find(resource.parameter_id);
            if (found != defaults->desc().texture_defaults.end())
            {
                textures.named_defaults[resource.default_value] = found->second;
            }
        }
        materials_ = std::make_unique<MaterialLibrary>(
            workspace_.types(), workspace_.files(),
            [this]() -> const AssetIndex&
            {
                return workspace_.catalog().index;
            },
            [this, defaults](const std::string& name, const std::vector<shader::ShaderPermutationSelection>& selections)
            {
                return shader_workflow_ready_
                           ? shaders_.shader_map(name, selections)
                           : (name == defaults->desc().shader_name && selections.empty() ? defaults->desc().shader_map
                                                                                         : nullptr);
            },
            std::move(textures));
        thumbnails_.set_material_resolver(
            [this](const AssetRef& reference)
            {
                return materials_->load(reference);
            });
        materials_->set_default_material(defaults);
        materials_->set_shader_diagnostic(
            [this](const std::string& name)
            {
                return shaders_.unavailable_reason(name);
            });
        material_assignments_.initialize(workspace_, *materials_);
        material_editor_.initialize(workspace_, actor_factory_.default_material()->material(),
                                    PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
        material_editor_.set_preview_pool(thumbnails_);
        animation_editor_.set_preview_mesh_changed(
            [this]()
            {
                thumbnails_.invalidate();
            });
        resource_picker_.set_builtin_resolver(
            [this](const std::string& kind)
            {
                return actor_factory_.instantiate_builtin(kind);
            });
        resource_picker_.set_browse(
            [this](const AssetId& id)
            {
                const auto* asset = workspace_.catalog().index.find(id);
                if (asset)
                {
                    asset_folder_ = asset->path.utf8().substr(0, asset->path.utf8().find_last_of('/'));
                    show_engine_content_ = asset_folder_.compare(0, 7, "/Engine") == 0 || show_engine_content_;
                    selection_.select_asset(id);
                }
            });
        material_editor_.edit_session().set_publish(
            [this](const AssetRef& reference)
            {
                const auto status = materials_->reload(reference);
                if (status.succeeded())
                {
                    thumbnails_.invalidate();
                }
                return status;
            });
        shaders_.set_material_validation_targets(
            [this](const std::vector<ShaderMapCollectionRef>& programs,
                   std::vector<MaterialShaderMapValidationTarget>& targets, std::string& error)
            {
                const auto status = materials_->collect_shader_validation_targets(programs, targets);
                if (!status.succeeded())
                {
                    error = status.message;
                    return false;
                }
                return material_editor_.collect_shader_validation_targets(programs, targets, error);
            });

        auto& arguments = CommandLineParser::get_instance();
        ShaderWorkflowPaths shader_paths;
        if (project_ && project_->active())
        {
            shader_paths.project_shader = project_->shader();
            shader_paths.project_build_settings = PhysicalPath(project_->config().utf8() + "/shader_build.settings");
        }
        shader_paths.engine_build_settings = PhysicalPath(TOY3D_EDITOR_ENGINE_CONFIG_ROOT "/shader_build.settings");
        shader_paths.engine_shader = PhysicalPath(TOY3D_EDITOR_ENGINE_SHADER_ROOT);
        shader_paths.engine_include = PhysicalPath(TOY3D_EDITOR_ENGINE_INCLUDE_ROOT);
        shader_paths.builtin_root = PhysicalPath(TOY3D_BUILTIN_SHADER_ROOT);
        shader_paths.saved = PhysicalPath(saved_root_ + "/shader");
        shader_paths.compiler = PhysicalPath(TOY3D_EDITOR_SHADER_COMPILER);
        shader_paths.toolchain = PhysicalPath(TOY3D_EDITOR_SHADER_TOOLCHAIN);
        shader_paths.code_executable = PhysicalPath(arguments.get_option("Editor.CodeExecutable", ""));
        std::string shader_error;
        shader_workflow_ready_ =
            shaders_.initialize(std::move(shader_paths), actor_factory_.default_material()->material(), shader_error);
        if (shader_workflow_ready_)
        {
            material_editor_.set_shader_workflow(shaders_);
            material_assignments_.set_shader_workflow(shaders_);
        }
        else
        {
            TOY_LOG_ERROR("Material source workflow unavailable: {}", shader_error);
        }
        startup_scene_ = ConsoleManager::get_instance().get_string("Editor.StartupScene");
        if (startup_scene_.empty())
        {
            startup_scene_ = "/Engine/Scenes/Default.scene";
        }
        return register_panels();
    }

    void EditorApplication::on_tick(double delta_seconds)
    {
        tick_play(delta_seconds);
        scene_session_.history().synchronize(world());
        thumbnails_.tick();
        mesh_bindings_.tick(world());
        const auto placed_actor = mesh_bindings_.take_placed_actor();
        if (placed_actor)
        {
            selection_.select_actor(world(), placed_actor);
            scene_viewport_.cancel_pending_hit();
        }
        texture_preview_.tick();
        animation_editor_.tick(delta_seconds);
        if (!play_session_.active())
        {
            tick_shaders();
        }
        if (startup_pending_ && (!shader_workflow_ready_ || !shaders_.busy()))
        {
            load_startup_scene();
        }
    }

    void EditorApplication::tick_shaders()
    {
        if (!shader_workflow_ready_)
        {
            return;
        }
        shaders_.tick();
        material_assignments_.tick_compile_assignment(world(), scene_session_.history(), material_assignment_error_);
        if (!shaders_.candidate_ready())
        {
            return;
        }
        auto& session = material_editor_.edit_session();
        if (shaders_.origin().valid() && (!session.active() || !(session.id() == shaders_.origin()) ||
                                          material_editor_.session_revision() != shaders_.origin_revision()))
        {
            shaders_.reject("The material session changed during compilation. Recompile from the current session.");
            return;
        }
        if (session.gesturing())
        {
            return;
        }
        std::string error;
        try
        {
            if (!shaders_.validate_candidate_users(error) ||
                !material_assignments_.prepare_shader(shaders_.candidate_configurations(), error) ||
                !material_editor_.prepare_shader(shaders_.candidate_configurations(), shaders_.candidate_properties(),
                                                 error))
            {
                material_assignments_.discard_shader();
                material_editor_.discard_shader();
                shaders_.reject(error);
                return;
            }
            // GT candidates/target identities and RT pipelines are checked
            // before publishing the source record and the slot references.
            if (!material_assignments_.publish_shader(error, true))
            {
                material_assignments_.discard_shader();
                material_editor_.discard_shader();
                shaders_.reject(error);
                return;
            }
            if (!shaders_.publish())
            {
                error = shaders_.error();
                material_assignments_.discard_shader();
                material_editor_.discard_shader();
                shaders_.reject(error);
                return;
            }
            material_assignments_.complete_shader();
            material_editor_.publish_shader();
            TOY_LOG_INFO("{}", shaders_.status());
        }
        catch (const std::exception& exception)
        {
            material_assignments_.discard_shader();
            material_editor_.discard_shader();
            shaders_.reject(exception.what());
        }
    }

    void EditorApplication::on_shutdown()
    {
        stop_play();
        play_scene_ = nullptr;
        panels_.clear();
        asset_editors_.clear();
        place_actors_.clear();
        content_browser_.clear();
        shaders_.shutdown();
        if (!window().enable_file_drop(false))
        {
            TOY_LOG_WARN("Could not disable external asset file drop.");
        }
        model_import_.clear();
        skeletal_import_.shutdown();
        material_create_.clear();
        material_editor_.shutdown();
        texture_preview_.shutdown();
        animation_editor_.shutdown();
        mesh_bindings_.shutdown();
        resource_picker_.clear();
        thumbnails_.shutdown();
        scene_viewport_.exit_camera_view();
        scene_session_.history().clear();
        for (const auto actor_id : world().actor_ids())
        {
            Actor* actor = world().find_actor_by_id(actor_id);
            if (actor && !world().destroy_actor(*actor))
            {
                TOY_LOG_ERROR("Editor Actor teardown failed.");
            }
        }
        const RenderFenceWaitResult drained = flush_rendering_commands();
        if (!drained.succeeded())
        {
            TOY_LOG_ERROR("Editor preview scene could not drain before material release: {}",
                          drained.framework_status().message);
        }
        scene_session_.history().cancel();
        selection_.clear_actor();
        selection_.clear_asset();
        material_assignments_.shutdown();
        if (materials_)
        {
            materials_->shutdown();
            materials_.reset();
        }
        actor_factory_.release();
    }

    bool EditorApplication::register_panels()
    {
        texture_preview_.set_reimport_callback(
            [this](const AssetId& id, TextureImportSettings settings)
            {
                if (!play_session_.active() && !model_import_.active() && !skeletal_import_.active() &&
                    !texture_import_.active() && !texture_import_.request_reimport(workspace_, id, settings))
                {
                    model_error_ = texture_import_.error();
                }
            });
        auto add_scene_panel = [this](const char* id, const char* title, const char* window, std::function<void()> draw)
        {
            EditorPanel panel;
            panel.id = id;
            panel.title = title;
            panel.window_name = window;
            panel.draw = [this, id, draw = std::move(draw)]()
            {
                const bool read_only = play_session_.active() && std::string(id) != "scene_viewport";
                ImGui::BeginDisabled(read_only);
                draw();
                ImGui::EndDisabled();
            };
            panel.undo = [this]()
            {
                apply_scene_history(false);
            };
            panel.redo = [this]()
            {
                apply_scene_history(true);
            };
            panel.save = [this]()
            {
                request_scene_save();
            };
            panel.focused = [window]()
            {
                const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
                return focused && focused->RootWindow == ImGui::FindWindowByName(window);
            };
            return panels_.add(std::move(panel));
        };
        if (!panels_.add({"place_actors",
                          "Place Actors",
                          "Place Actors",
                          [this]()
                          {
                              ImGui::BeginDisabled(play_session_.active());
                              place_actors_.draw(&actor_factory_.actor_types());
                              ImGui::EndDisabled();
                          },
                          {},
                          {},
                          {}}) ||
            !add_scene_panel("outliner", "Outliner", "Outliner",
                             [this]()
                             {
                                 if (draw_outliner(world(), selection_, scene_session_.history(), actor_factory_,
                                                   scene_viewport_))
                                 {
                                     scene_viewport_.cancel_pending_hit();
                                 }
                             }) ||
            !add_scene_panel("details", "Details", "Details",
                             [this]()
                             {
                                 draw_details(world(), selection_, scene_session_.history(), workspace_,
                                              scene_viewport_, material_assignments_, material_assignment_error_,
                                              &resource_picker_, &mesh_bindings_);
                             }) ||
            !add_scene_panel("world_settings", "World Settings", "World Settings",
                             [this]()
                             {
                                 draw_world_settings(world(), workspace_, scene_session_.history(),
                                                     material_assignment_error_);
                             }) ||
            !add_scene_panel("scene_viewport", "Scene Viewport", "Scene Viewport###Game Viewport",
                             [this]()
                             {
                                 scene_viewport_.draw(world(), selection_, scene_session_.history(), &play_session_,
                                                      can_start_play());
                             }) ||
            !panels_.add({"content_browser",
                          "Content Browser",
                          "Content Browser",
                          [this]()
                          {
                              draw_asset_browser();
                          },
                          {},
                          {},
                          {}}) ||
            !panels_.add({"texture_preview",
                          "Texture Preview",
                          "Texture Preview",
                          [this]()
                          {
                              texture_preview_.draw();
                          },
                          {},
                          {},
                          {}}) ||
            !panels_.add({"animation_editor",
                          "Animation Editor",
                          "Animation Editor",
                          [this]()
                          {
                              animation_editor_.draw();
                          },
                          {},
                          {},
                          {}}) ||
            !panels_.add({"material_editor", "Material Editor", "Material Editor",
                          [this]()
                          {
                              ImGui::BeginDisabled(play_session_.active());
                              material_editor_.draw();
                              ImGui::EndDisabled();
                          },
                          [this]()
                          {
                              material_editor_.undo();
                          },
                          [this]()
                          {
                              material_editor_.redo();
                          },
                          [this]()
                          {
                              return material_editor_.focused();
                          },
                          [this]()
                          {
                              material_editor_.save();
                          }}))
        {
            return false;
        }
        EditorPanel console;
        console.id = "console";
        console.title = "Console";
        console.window_name = "Console";
        console.draw = [this]()
        {
            console_.draw();
        };
        console.open = [this]()
        {
            console_.open();
        };
        if (!panels_.add(std::move(console)))
        {
            return false;
        }
        panels_.freeze();
        auto material_open = [this](const AssetId& id, bool)
        {
            material_editor_.request_open(id);
        };
        if (!asset_editors_.add({"toy3d.SceneAssetData",
                                 [this](const AssetId& id, bool)
                                 {
                                     request_scene_action(SceneAction::Open, id);
                                 }}) ||
            !asset_editors_.add({"toy3d.MaterialAssetData", material_open}) ||
            !asset_editors_.add({"toy3d.MaterialInstanceAssetData", material_open}) ||
            !asset_editors_.add({"toy3d.Texture2DAssetData", [this](const AssetId& id, bool focus)
                                 {
                                     texture_preview_.request_open(id, focus);
                                 }}))
        {
            return false;
        }
        for (const char* type :
             {"toy3d.SkeletonAssetData", "toy3d.SkeletalMeshAssetData", "toy3d.AnimationSequenceAssetData"})
        {
            if (!asset_editors_.add({type, [this](const AssetId& id, bool focus)
                                     {
                                         animation_editor_.request_open(id, focus);
                                     }}))
            {
                return false;
            }
        }
        asset_editors_.freeze();
        return true;
    }

    void EditorApplication::draw_asset_browser()
    {
        ImGui::BeginDisabled(play_session_.active());
        const ContentBrowserActions browser =
            content_browser_.draw(workspace_, selection_, asset_folder_, show_engine_content_, thumbnails_,
                                  WITH_MODEL_IMPORT != 0 && workspace_.has_project());
        ImGui::EndDisabled();
        if (play_session_.active())
        {
            FileDropEvent discarded;
            while (window().take_file_drop(discarded))
            {
            }
            return;
        }
        if (browser.assets_refreshed)
        {
            texture_preview_.invalidate();
            animation_editor_.invalidate();
        }
        if (browser.asset_open.valid() && !model_import_.active() && !skeletal_import_.active() &&
            !texture_import_.active() && (!material_create_.active() && !shader_create_.active()))
        {
            const auto* asset = workspace_.catalog().index.find(browser.asset_open);
            if (!asset || !asset_editors_.request_open(asset->index.root_type, browser.asset_open, browser.asset_focus))
            {
                model_error_ = "This asset has no registered editor.";
                TOY_LOG_ERROR("Open asset [{}]: {}", browser.asset_open.hex(), model_error_);
            }
        }
        if (workspace_.has_project() && browser.material_creation_requested && !model_import_.active() &&
            !skeletal_import_.active() && !texture_import_.active() && !shader_create_.active())
        {
            material_create_.request(browser.material_creation_kind, asset_folder_, browser.material_parent);
        }
        if (workspace_.has_project() && browser.texture_import_requested &&
            (!material_create_.active() && !shader_create_.active()) && !model_import_.active() &&
            !skeletal_import_.active() && !texture_import_.request(asset_folder_))
        {
            model_error_ = texture_import_.error();
            TOY_LOG_ERROR("Request Texture2D import: {}", model_error_);
        }
#if WITH_MODEL_IMPORT
        if (workspace_.has_project() && !material_create_.active() && !shader_create_.active() &&
            !texture_import_.active() && !model_import_.active() && !skeletal_import_.active())
        {
            if ((browser.skeletal_import_requested || browser.animation_import_requested) &&
                !skeletal_import_.request(asset_folder_, browser.animation_import_requested))
            {
                model_error_ = skeletal_import_.error();
            }
            if (browser.skeletal_reimport.valid() &&
                !skeletal_import_.request_reimport(workspace_, browser.skeletal_reimport))
            {
                model_error_ = skeletal_import_.error();
            }
        }
        if (workspace_.has_project() && browser.import_requested &&
            (!material_create_.active() && !shader_create_.active()) && !texture_import_.active() &&
            !skeletal_import_.active() && !model_import_.request(asset_folder_))
        {
            model_error_ = model_import_.error();
            TOY_LOG_ERROR("Request model import: {}", model_error_);
        }
#endif
        if (workspace_.has_project() && browser.environment_import_requested && !material_create_.active() &&
            !shader_create_.active() && !model_import_.active() && !skeletal_import_.active() &&
            !texture_import_.request_environment(asset_folder_))
        {
            model_error_ = texture_import_.error();
        }
        FileDropEvent dropped;
        while (window().take_file_drop(dropped))
        {
            if (!workspace_.has_project() || model_import_.active() || skeletal_import_.active() ||
                texture_import_.active() || material_create_.active() || shader_create_.active() ||
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || !browser.accepts_drop(dropped.position))
            {
                continue;
            }
            bool environment = false;
            bool image = false;
            bool model = false;
            for (const auto& path : dropped.paths)
            {
                std::string extension =
                    path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c)
                               {
                                   return static_cast<char>(std::tolower(c));
                               });
                if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
                {
                    image = true;
                }
                else if (extension == ".hdr")
                {
                    environment = true;
                }
                else
                {
                    model = true;
                }
            }
            if ((image && model) || (environment && (image || model)))
            {
                model_error_ = "Drop HDR environments, color/data images and model files separately.";
                TOY_LOG_ERROR("Asset drop: {}", model_error_);
            }
            else if (environment && !texture_import_.request_environment(asset_folder_, dropped.paths))
            {
                model_error_ = texture_import_.error();
            }
            else if (image && !texture_import_.request(asset_folder_, dropped.paths))
            {
                model_error_ = texture_import_.error();
                TOY_LOG_ERROR("Request Texture2D import: {}", model_error_);
            }
#if WITH_MODEL_IMPORT
            else if (model && !model_import_.request(asset_folder_, dropped.paths))
            {
                model_error_ = model_import_.error();
                TOY_LOG_ERROR("Request model import: {}", model_error_);
            }
#else
            else if (model)
            {
                model_error_ = "Model import is disabled in this build.";
                TOY_LOG_ERROR("Asset drop: {}", model_error_);
            }
#endif
        }
    }

    void EditorApplication::on_build_ui()
    {
        scene_viewport_.begin_frame();
        // Observe a completed task before a menu/panel can start its successor
        // in this same frame; otherwise its previous card would remain active.
        notifications_.update(shaders_.task_status());
        if (!startup_pending_)
        {
            draw_main_menu();
        }
        if (ImGui::BeginPopupModal("About Toy3d Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Toy3d Editor");
            ImGui::TextUnformatted("Scene and asset workspace");
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        const ImGuiViewport* const viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        constexpr ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                ImGuiWindowFlags_NoNavFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Toy3d Editor Dockspace", nullptr, host_flags);
        ImGui::PopStyleVar(3);
        const ImGuiID dockspace = ImGui::GetID("Toy3d Editor Dockspace Node");
        if (reset_dock_layout_)
        {
            ImGui::DockBuilderRemoveNode(dockspace);
            initial_dock_layout_checked_ = false;
            reset_dock_layout_ = false;
        }
        if (!initial_dock_layout_checked_)
        {
            initial_dock_layout_checked_ = true;
            if (ImGui::DockBuilderGetNode(dockspace) == nullptr || ImGui::FindWindowByName("Place Actors") == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dockspace);
                ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
                ImVec2 dock_size = ImGui::GetContentRegionAvail();
                dock_size.y = dock_size.y > 28.0f ? dock_size.y - 28.0f : 0.0f;
                ImGui::DockBuilderSetNodeSize(dockspace, dock_size);
                ImGuiID content_dock = 0;
                ImGuiID upper_dock = 0;
                ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.27f, &content_dock, &upper_dock);
                ImGuiID right_dock = 0;
                ImGuiID scene_dock = 0;
                ImGui::DockBuilderSplitNode(upper_dock, ImGuiDir_Right, 0.25f, &right_dock, &scene_dock);
                ImGuiID placement_dock = 0;
                ImGui::DockBuilderSplitNode(scene_dock, ImGuiDir_Left, 0.22f, &placement_dock, &scene_dock);
                ImGui::DockBuilderDockWindow("Place Actors", placement_dock);
                ImGuiID outliner_dock = 0;
                ImGuiID details_dock = 0;
                ImGui::DockBuilderSplitNode(right_dock, ImGuiDir_Up, 0.55f, &outliner_dock, &details_dock);
                ImGui::DockBuilderDockWindow("Scene Viewport###Game Viewport", scene_dock);
                ImGui::DockBuilderDockWindow("Outliner", outliner_dock);
                ImGui::DockBuilderDockWindow("Details", details_dock);
                ImGui::DockBuilderDockWindow("World Settings", details_dock);
                ImGui::DockBuilderDockWindow("Texture Preview", details_dock);
                ImGui::DockBuilderDockWindow("Content Browser", content_dock);
                ImGui::DockBuilderDockWindow("Console", content_dock);
                ImGui::DockBuilderFinish(dockspace);
            }
        }
        ImGui::DockSpace(dockspace, ImVec2(0.0f, -28.0f));
        ImGui::Separator();
        ImGui::Text("Assets: %u", static_cast<unsigned>(workspace_.catalog().entries.size()));
        ImGui::SameLine();
        const std::string console_status = "Console E:" + std::to_string(console_.error_count()) +
                                           " W:" + std::to_string(console_.warning_count()) + "###ConsoleStatus";
        if (ImGui::SmallButton(console_status.c_str()))
        {
            console_.open();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Scene: %s%s",
                            scene_session_.path().empty() ? "Untitled" : scene_session_.path().utf8().c_str(),
                            scene_dirty() ? " *" : "");
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Project: %s",
                            project_ && project_->active() ? project_->description().name.c_str() : "No Project");
        if (!workspace_.error().empty())
        {
            ImGui::SameLine();
            ImGui::Text("  |  Asset scan failed: %s", workspace_.error().c_str());
        }
        ImGui::End();

        if (startup_pending_)
        {
            // Shader publication validates against real scene attachments. Build the
            // docked viewport while loading, before allowing scene editing.
            ImGui::SetNextWindowCollapsed(false);
            ImGui::SetNextWindowFocus();
            ImGui::BeginDisabled();
            scene_viewport_.draw(world(), selection_, scene_session_.history());
            ImGui::EndDisabled();
            if (ImGui::Begin("Opening Scene"))
            {
                ImGui::TextUnformatted("Preparing shaders and scene resources...");
                ImGui::TextUnformatted(startup_scene_.c_str());
            }
            ImGui::End();
            console_.draw();
            notifications_.draw(console_, shaders_);
            return;
        }
        panels_.draw();
#if WITH_MODEL_IMPORT
        model_import_.draw(window(), workspace_, selection_, thumbnails_);
        const bool skeletal_import_active = skeletal_import_.active();
        skeletal_import_.draw(window(), workspace_, selection_, thumbnails_);
        if (skeletal_import_active && !skeletal_import_.active())
        {
            animation_editor_.invalidate();
        }
#endif
        texture_import_.draw(window(), workspace_, selection_);
        material_create_.draw(workspace_, selection_, asset_folder_,
                              actor_factory_.default_material()->material()->parameter_schema(),
                              shader_workflow_ready_ ? &shaders_ : nullptr);
        shader_create_.draw(shaders_, notifications_);
        draw_scene_dialogs();
        draw_project_dialogs();
        const auto locate = material_editor_.take_locate_parent();
        if (locate.valid())
        {
            const auto* location = workspace_.catalog().index.find(locate);
            if (location)
            {
                selection_.select_asset(locate);
                asset_folder_ = location->path.utf8().substr(0, location->path.utf8().find_last_of('/'));
                if (asset_folder_.compare(0, 7, "/Engine") == 0)
                {
                    show_engine_content_ = true;
                }
            }
        }
        const bool material_exit = material_editor_.take_exit();
        if (waiting_material_project_)
        {
            if (material_exit)
            {
                waiting_material_project_ = false;
                request_scene_action(SceneAction::SwitchProject);
            }
            else if (!material_editor_.modal_pending())
            {
                waiting_material_project_ = false;
                pending_project_ = {};
                project_scene_saved_ = false;
            }
        }
        else if (material_exit)
        {
            window().close();
        }
        AssetPlacementRequest placed;
        if (scene_viewport_.take_asset_placement(placed))
        {
            if (!mesh_bindings_.place(world(), placed))
            {
                model_error_ = mesh_bindings_.error();
                TOY_LOG_ERROR("Asset placement failed: {}", model_error_);
            }
        }
        if (!model_error_.empty())
        {
            if (ImGui::Begin("Model Import / Load"))
            {
                ImGui::TextWrapped("%s", model_error_.c_str());
                if (ImGui::Button("Dismiss"))
                {
                    model_error_.clear();
                }
            }
            ImGui::End();
        }

        selection_.resolve_actor(world());
        const ImGuiIO& io = ImGui::GetIO();
        const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
        if (focused)
        {
            focused = focused->RootWindow;
        }
        const bool actor_panel_focused =
            focused &&
            (focused == ImGui::FindWindowByName("Scene Viewport###Game Viewport") ||
             focused == ImGui::FindWindowByName("Outliner") || focused == ImGui::FindWindowByName("Details"));
        const bool modal_active =
            model_import_.active() || skeletal_import_.active() || texture_import_.active() || show_new_project_ ||
            show_project_settings_ || show_scene_save_as_ || material_create_.active() || shader_create_.active() ||
            material_editor_.modal_pending() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        // Keep Stop available even when the viewport tab is hidden/collapsed.
        if (play_session_.active() && !modal_active && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            play_session_.request(EditorPlayAction::Stop);
            scene_viewport_.release_game_input();
        }
        if (!play_session_.active() && !modal_active && actor_panel_focused &&
            selection_.focus() == EditorSelectionFocus::Actor && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (scene_session_.history().delete_actor(world(), selection_.actor_id()))
            {
                selection_.clear_actor();
                scene_viewport_.cancel_pending_hit();
            }
        }
        panels_.process_shortcuts(play_session_.active() || model_import_.active() || skeletal_import_.active() ||
                                  texture_import_.active() || show_new_project_ || show_project_settings_ ||
                                  show_scene_save_as_ || material_create_.active() || shader_create_.active() ||
                                  material_editor_.modal_pending());
        notifications_.update(shaders_.task_status());
        notifications_.draw(console_, shaders_);
    }

    void EditorApplication::apply_scene_history(bool redo)
    {
        if (play_session_.active())
        {
            return;
        }
        auto& history = scene_session_.history();
        const bool applied = redo ? history.redo(world()) : history.undo(world());
        if (!applied && !history.error().empty())
        {
            scene_error_ = history.error();
            TOY_LOG_ERROR("Scene history: {}", scene_error_);
        }
        else if (applied)
        {
            scene_error_.clear();
        }
    }

    void EditorApplication::undo_edit()
    {
        if (play_session_.active())
        {
            return;
        }
        if (ImGui::IsAnyItemActive() || model_import_.active() || skeletal_import_.active() ||
            texture_import_.active() || show_new_project_ || show_project_settings_ || show_scene_save_as_ ||
            material_create_.active() || shader_create_.active() || material_editor_.modal_pending())
        {
            return;
        }
        panels_.undo();
    }

    void EditorApplication::redo_edit()
    {
        if (play_session_.active())
        {
            return;
        }
        if (ImGui::IsAnyItemActive() || model_import_.active() || skeletal_import_.active() ||
            texture_import_.active() || show_new_project_ || show_project_settings_ || show_scene_save_as_ ||
            material_create_.active() || shader_create_.active() || material_editor_.modal_pending())
        {
            return;
        }
        panels_.redo();
    }

    bool EditorApplication::on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks)
    {
        mesh_bindings_.initialize(tasks);
        return thumbnails_.initialize(scene, actor_factory_.default_material(), tasks);
    }

    bool EditorApplication::on_initialize_animation_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks)
    {
        return animation_editor_.initialize(scene, actor_factory_.default_material(), tasks);
    }

    bool EditorApplication::on_hit_proxy_request(HitProxyRequest& request)
    {
        if (play_session_.active())
        {
            return false;
        }
        return scene_viewport_.take_hit_request(request);
    }

    void EditorApplication::on_hit_proxy_result(const HitProxyResult& result)
    {
        if (play_session_.active())
        {
            return;
        }
        scene_viewport_.receive_hit_result(world(), selection_, result);
    }

    bool EditorApplication::on_scene_viewport_extent(Extent& extent) const
    {
        return scene_viewport_.extent(extent);
    }

    void EditorApplication::on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const
    {
        if (play_session_.active() && play_session_.world())
        {
            build_game_scene_views(*play_session_.world(), views, extent);
        }
        else
        {
            scene_viewport_.build_scene_views(world(), views, extent);
        }
    }
} // namespace toy3d
