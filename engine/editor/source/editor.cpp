#include "editor.h"

#include <exception>
#include <algorithm>
#include <cctype>

#include "imgui.h"
#include "imgui_internal.h"

#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "math/quaternion.h"
#include "panels/place_actors_panel.h"
#include "panels/scene_panels.h"
#include "panels/content_browser_panel.h"
#include "rendercore/frame_synchronization.h"
#include "workspace/editor_workspace.h"
#include "placement/asset_placement.h"
#include "config/command_line_parser.h"

namespace toy3d
{
    bool EditorApplication::on_initialize()
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return false;
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.TabRounding = 3.0f;
        ImGui::GetIO().IniFilename = TOY3D_EDITOR_LAYOUT_PATH;
        // Scene-image gestures edit content; dock panels move by their title/tab.
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
        if (!window().enable_file_drop(true)) TOY_LOG_WARN("External asset file drop is unavailable on this platform.");
        if (!actor_factory_.initialize()) return false;
        MaterialTextureValues textures;
        const auto defaults = actor_factory_.default_material()->material();
        for (const auto& resource : defaults->parameter_schema().resources)
        {
            const auto found = defaults->desc().texture_defaults.find(resource.parameter_id);
            if (found != defaults->desc().texture_defaults.end()) textures.named_defaults[resource.default_value] = found->second;
        }
        materials_ = std::make_unique<MaterialLibrary>(workspace_.types(), workspace_.files(),
            [this]() -> const AssetIndex& { return workspace_.catalog().index; },
            [this, defaults](const std::string& name)
            {
                return shader_workflow_ready_ ? shaders_.program(name) :
                    (name == defaults->desc().shader_name ? defaults->desc().shader_program : nullptr);
            }, std::move(textures));
        material_assignments_.initialize(workspace_, *materials_);
        workspace_.material_edit().set_publish([this](const AssetRef& reference) { return materials_->reload(reference); });
        material_editor_.initialize(workspace_, actor_factory_.default_material()->material(),
            PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT));
        auto& arguments = CommandLineParser::get_instance();
        MaterialShaderPaths shader_paths;
        shader_paths.project_shader = PhysicalPath(arguments.get_option("Editor.ProjectShaderRoot", TOY3D_EDITOR_PROJECT_SHADER_ROOT));
        shader_paths.project_config = PhysicalPath(arguments.get_option("Editor.ShaderConfigRoot", TOY3D_EDITOR_PROJECT_CONFIG_ROOT));
        shader_paths.engine_shader = PhysicalPath(TOY3D_EDITOR_ENGINE_SHADER_ROOT);
        shader_paths.engine_include = PhysicalPath(TOY3D_EDITOR_ENGINE_INCLUDE_ROOT);
        shader_paths.builtin_entries = PhysicalPath(TOY3D_SHADER_MAP_ENTRY_ROOT);
        shader_paths.saved = PhysicalPath(TOY3D_EDITOR_SHADER_SAVED_ROOT);
        shader_paths.compiler = PhysicalPath(TOY3D_EDITOR_SHADER_COMPILER);
        shader_paths.toolchain = PhysicalPath(TOY3D_EDITOR_SHADER_TOOLCHAIN);
        shader_paths.code_executable = PhysicalPath(arguments.get_option("Editor.CodeExecutable", ""));
        std::string shader_error;
        shader_workflow_ready_ = shaders_.initialize(std::move(shader_paths), actor_factory_.default_material()->material(), shader_error);
        if (shader_workflow_ready_)
        {
            material_editor_.set_shader_workflow(shaders_);

        }
        else TOY_LOG_ERROR("Material source workflow unavailable: {}", shader_error);
        PlacementRequest preview;
        preview.item = PlacementItemId::Cube;
        preview.transform.translation = Vector3(0.0f, 0.75f, 3.0f);
        if (!actor_factory_.create(world(), preview)) return false;
        preview.item = PlacementItemId::DirectionalLight;
        preview.transform.translation = Vector3(0.0f, 3.0f, 3.0f);
        if (!try_make_rotation_from_forward_up(Vector3(-0.35f, -0.55f, 0.75f),
                                                Vector3(0, 1, 0), preview.transform.rotation)) return false;
        if (!actor_factory_.create(world(), preview)) return false;
        return true;
    }

    void EditorApplication::on_tick(double)
    {
        thumbnails_.tick();
        if (!shader_workflow_ready_) return;
        shaders_.tick();
        if (!shaders_.candidate_ready()) return;
        auto& session = workspace_.material_edit();
        if (shaders_.origin().valid() && (!session.active() || !(session.id() == shaders_.origin()) ||
            material_editor_.session_revision() != shaders_.origin_revision()))
        { shaders_.reject("The material session changed during compilation. Recompile from the current session."); return; }
        if (session.gesturing()) return;
        std::string error;
        try
        {
            if (!material_assignments_.prepare_shader(shaders_.candidate(), error) ||
                !material_editor_.prepare_shader(shaders_.candidate(), shaders_.candidate_properties(), error))
            {
                material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(error); return;
            }
            // GT candidates/target identities and RT pipelines are checked
            // before publishing the source record and the slot references.
            if (!material_assignments_.publish_shader(error, true))
            { material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(error); return; }
            if (!shaders_.publish())
            {
                error = shaders_.error(); material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(error); return;
            }
            material_assignments_.complete_shader();
            material_editor_.publish_shader();
            TOY_LOG_INFO("{}", shaders_.status());
        }
        catch (const std::exception& exception)
        { material_assignments_.discard_shader(); material_editor_.discard_shader(); shaders_.reject(exception.what()); }
    }

    void EditorApplication::on_shutdown()
    {
        shaders_.shutdown();
        if (!window().enable_file_drop(false)) TOY_LOG_WARN("Could not disable external asset file drop.");
        model_import_.clear();
        material_create_.clear();
        material_editor_.shutdown();
        thumbnails_.shutdown();
        scene_viewport_.exit_camera_view();
        command_history_.clear();
        for (const auto actor_id : world().actor_ids())
        {
            Actor* actor = world().find_actor_by_id(actor_id);
            if (actor && !world().destroy_actor(*actor)) TOY_LOG_ERROR("Editor Actor teardown failed.");
        }
        const RenderFenceWaitResult drained = flush_rendering_commands();
        if (!drained.succeeded())
            TOY_LOG_ERROR("Editor preview scene could not drain before material release: {}",
                          drained.framework_status().message);
        command_history_.cancel();
        selection_.clear_actor();
        selection_.clear_asset();
        material_assignments_.shutdown();
        workspace_.material_edit().set_publish({});
        if (materials_) { materials_->shutdown(); materials_.reset(); }
        actor_factory_.release();
    }

    void EditorApplication::on_build_ui()
    {
        scene_viewport_.begin_frame();
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::BeginMenu("Create Asset", !model_import_.active() && !texture_import_.active() && !material_create_.active()))
                {
                    if (ImGui::MenuItem("Material...")) material_create_.request(MaterialAssetCreationKind::Material, asset_folder_);
                    if (ImGui::MenuItem("Material Instance...")) material_create_.request(MaterialAssetCreationKind::MaterialInstance, asset_folder_);
                    ImGui::EndMenu();
                }
#if WITH_MODEL_IMPORT
                if (ImGui::MenuItem("Import Static Mesh...", nullptr, false, !material_create_.active()))
                {
                    if (!model_import_.request(asset_folder_)) model_error_ = model_import_.error();
                }
#endif
                if (ImGui::MenuItem("Exit")) window().close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) undo_edit();
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) redo_edit();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Reset Layout")) reset_dock_layout_ = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Scene Viewport")) ImGui::SetWindowFocus("Scene Viewport###Game Viewport");
                if (ImGui::MenuItem("Place Actors")) ImGui::SetWindowFocus("Place Actors");
                if (ImGui::MenuItem("Outliner")) ImGui::SetWindowFocus("Outliner");
                if (ImGui::MenuItem("Details")) ImGui::SetWindowFocus("Details");
                if (ImGui::MenuItem("Content Browser")) ImGui::SetWindowFocus("Content Browser");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About Toy3d Editor")) ImGui::OpenPopup("About Toy3d Editor");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (ImGui::BeginPopupModal("About Toy3d Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Toy3d Editor");
            ImGui::TextUnformatted("Scene and asset workspace");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
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
        ImGui::TextUnformatted("TOY3D EDITOR");
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Scene");
        ImGui::SameLine();
        if (ImGui::Button("Undo")) undo_edit();
        ImGui::SameLine();
        if (ImGui::Button("Redo")) redo_edit();
        ImGui::SameLine();
        if (ImGui::Button("Refresh Assets"))
        {
            if (!workspace_.refresh()) TOY_LOG_ERROR("Editor asset refresh failed: {}", workspace_.error());
            else thumbnails_.invalidate();
        }
        ImGui::Separator();
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
            if (ImGui::DockBuilderGetNode(dockspace) == nullptr ||
                ImGui::FindWindowByName("Place Actors") == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dockspace);
                ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
                ImVec2 dock_size = ImGui::GetContentRegionAvail();
                dock_size.y = dock_size.y > 28.0f ? dock_size.y - 28.0f : 0.0f;
                ImGui::DockBuilderSetNodeSize(dockspace, dock_size);
                ImGuiID content_dock = 0;
                ImGuiID upper_dock = 0;
                ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.27f,
                                            &content_dock, &upper_dock);
                ImGuiID right_dock = 0;
                ImGuiID scene_dock = 0;
                ImGui::DockBuilderSplitNode(upper_dock, ImGuiDir_Right, 0.25f,
                                            &right_dock, &scene_dock);
                ImGuiID placement_dock = 0;
                ImGui::DockBuilderSplitNode(scene_dock, ImGuiDir_Left, 0.22f, &placement_dock, &scene_dock);
                ImGui::DockBuilderDockWindow("Place Actors", placement_dock);
                ImGuiID outliner_dock = 0;
                ImGuiID details_dock = 0;
                ImGui::DockBuilderSplitNode(right_dock, ImGuiDir_Up, 0.55f,
                                            &outliner_dock, &details_dock);
                ImGui::DockBuilderDockWindow("Scene Viewport###Game Viewport", scene_dock);
                ImGui::DockBuilderDockWindow("Outliner", outliner_dock);
                ImGui::DockBuilderDockWindow("Details", details_dock);
                ImGui::DockBuilderDockWindow("Content Browser", content_dock);
                ImGui::DockBuilderFinish(dockspace);
            }
        }
        ImGui::DockSpace(dockspace, ImVec2(0.0f, -28.0f));
        ImGui::Separator();
        ImGui::Text("Assets: %u", static_cast<unsigned>(workspace_.catalog().entries.size()));
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Source: %s", workspace_.source_root().utf8().c_str());
        if (!workspace_.error().empty())
        {
            ImGui::SameLine();
            ImGui::Text("  |  Asset scan failed: %s", workspace_.error().c_str());
        }
        ImGui::End();

        draw_place_actors_panel();
        if (draw_outliner(world(), selection_, command_history_, actor_factory_))
            scene_viewport_.cancel_pending_hit();
        draw_details(world(), selection_, command_history_, workspace_, scene_viewport_, material_assignments_, material_assignment_error_);
        scene_viewport_.draw(world(), selection_, command_history_);
        const ContentBrowserActions browser = draw_content_browser(workspace_, selection_, asset_folder_,
            show_engine_content_, thumbnails_, asset_tile_size_, WITH_MODEL_IMPORT != 0);
        if (browser.material_open.valid() && !model_import_.active() && !texture_import_.active() && !material_create_.active())
            material_editor_.request_open(browser.material_open);
        if (browser.material_creation_requested && !model_import_.active() && !texture_import_.active())
            material_create_.request(browser.material_creation_kind, asset_folder_, browser.material_parent);
        if (browser.texture_import_requested && !material_create_.active() && !model_import_.active() &&
            !texture_import_.request(asset_folder_)) model_error_ = texture_import_.error();
#if WITH_MODEL_IMPORT
        if (browser.import_requested && !material_create_.active() && !texture_import_.active() &&
            !model_import_.request(asset_folder_)) model_error_ = model_import_.error();
#endif
                if (ImGui::MenuItem("Import Texture2D...", nullptr, false,
                    !model_import_.active() && !material_create_.active()))
                {
                    if (!texture_import_.request(asset_folder_)) model_error_ = texture_import_.error();
                }
        FileDropEvent dropped;
        while (window().take_file_drop(dropped))
        {
            if (model_import_.active() || texture_import_.active() || material_create_.active() ||
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || !browser.accepts_drop(dropped.position)) continue;
            bool image = false;
            bool model = false;
            for (const auto& path : dropped.paths)
            {
                std::string extension = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") image = true;
                else model = true;
            }
            if (image && model) model_error_ = "Drop image and model files separately.";
            else if (image && !texture_import_.request(asset_folder_, dropped.paths)) model_error_ = texture_import_.error();
#if WITH_MODEL_IMPORT
            else if (model && !model_import_.request(asset_folder_, dropped.paths)) model_error_ = model_import_.error();
#else
            else if (model) model_error_ = "Model import is disabled in this build.";
#endif
        }
#if WITH_MODEL_IMPORT
        model_import_.draw(window(), workspace_, selection_, thumbnails_);
#endif
        texture_import_.draw(window(), workspace_, selection_);
        material_create_.draw(workspace_, selection_, asset_folder_, actor_factory_.default_material()->material()->parameter_schema(),
            shader_workflow_ready_ ? &shaders_ : nullptr);
        material_editor_.draw();
        const auto locate = material_editor_.take_locate_parent();
        if (locate.valid())
        {
            const auto* location = workspace_.catalog().index.find(locate);
            if (location)
            {
                selection_.select_asset(locate);
                asset_folder_ = location->path.utf8().substr(0, location->path.utf8().find_last_of('/'));
                if (asset_folder_.compare(0, 7, "/Engine") == 0) show_engine_content_ = true;
            }
        }
        if (material_editor_.take_exit()) window().close();
        AssetPlacementRequest placed;
        if (scene_viewport_.take_asset_placement(placed))
        {
            const std::uint32_t actor_id = place_static_mesh_asset(workspace_, world(), actor_factory_,
                command_history_, placed, model_error_);
            if (actor_id)
            {
                selection_.select_actor(world(), actor_id);
                scene_viewport_.cancel_pending_hit();
            }
            else TOY_LOG_ERROR("Asset placement failed: {}", model_error_);
        }
        if (!model_error_.empty())
        {
            if (ImGui::Begin("Model Import / Load"))
            {
                ImGui::TextWrapped("%s", model_error_.c_str());
                if (ImGui::Button("Dismiss")) model_error_.clear();
            }
            ImGui::End();
        }

        selection_.resolve_actor(world());
        const ImGuiIO& io = ImGui::GetIO();
        const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
        if (focused) focused = focused->RootWindow;
        const bool actor_panel_focused = focused &&
            (focused == ImGui::FindWindowByName("Scene Viewport###Game Viewport") ||
             focused == ImGui::FindWindowByName("Outliner") || focused == ImGui::FindWindowByName("Details"));
        if (material_editor_.focused()) material_history_target_ = true;
        else if (actor_panel_focused) material_history_target_ = false;
        const bool modal_active = model_import_.active() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        if (!modal_active && actor_panel_focused && selection_.focus() == EditorSelectionFocus::Actor &&
            !io.WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (command_history_.delete_actor(world(), selection_.actor_id()))
            {
                selection_.clear_actor();
                scene_viewport_.cancel_pending_hit();
            }
        }
        if (!modal_active && actor_panel_focused && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z))
            {
                if (io.KeyShift)
                    command_history_.redo(world());
                else
                    command_history_.undo(world());
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y))
                command_history_.redo(world());
        }
    }

    void EditorApplication::undo_edit()
    {
        if (ImGui::IsAnyItemActive() || model_import_.active() || material_create_.active() || material_editor_.modal_pending()) return;
        if (material_history_target_) material_editor_.undo();
        else command_history_.undo(world());
    }

    void EditorApplication::redo_edit()
    {
        if (ImGui::IsAnyItemActive() || model_import_.active() || material_create_.active() || material_editor_.modal_pending()) return;
        if (material_history_target_) material_editor_.redo();
        else command_history_.redo(world());
    }

    bool EditorApplication::on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks)
    {
        return thumbnails_.initialize(scene, actor_factory_.default_material(), tasks);
    }

    bool EditorApplication::on_hit_proxy_request(HitProxyRequest& request)
    {
        return scene_viewport_.take_hit_request(request);
    }

    void EditorApplication::on_hit_proxy_result(const HitProxyResult& result)
    {
        scene_viewport_.receive_hit_result(world(), selection_, result);
    }

    bool EditorApplication::on_scene_viewport_extent(Extent& extent) const
    {
        return scene_viewport_.extent(extent);
    }

    void EditorApplication::on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const
    {
        scene_viewport_.build_scene_views(world(), views, extent);
    }
} // namespace toy3d
