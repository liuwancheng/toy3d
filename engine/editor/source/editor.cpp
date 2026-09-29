#include "editor.h"

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
#if WITH_MODEL_IMPORT
        if (!window().enable_file_drop(true)) TOY_LOG_WARN("External model file drop is unavailable on this platform.");
#endif
        if (!actor_factory_.initialize()) return false;
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

    void EditorApplication::on_shutdown()
    {
#if WITH_MODEL_IMPORT
        if (!window().enable_file_drop(false)) TOY_LOG_WARN("Could not disable external model file drop.");
#endif
        model_import_.clear();
        material_create_.clear();
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
        actor_factory_.release();
    }

    void EditorApplication::on_build_ui()
    {
        scene_viewport_.begin_frame();
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::BeginMenu("Create Asset", !model_import_.active() && !material_create_.active()))
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
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) command_history_.undo(world());
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) command_history_.redo(world());
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
        if (ImGui::Button("Undo")) command_history_.undo(world());
        ImGui::SameLine();
        if (ImGui::Button("Redo")) command_history_.redo(world());
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
        draw_details(world(), selection_, command_history_, workspace_, scene_viewport_);
        scene_viewport_.draw(world(), selection_, command_history_);
        const ContentBrowserActions browser = draw_content_browser(workspace_, selection_, asset_folder_,
            show_engine_content_, thumbnails_, asset_tile_size_, WITH_MODEL_IMPORT != 0);
        if (browser.material_creation_requested && !model_import_.active())
            material_create_.request(browser.material_creation_kind, asset_folder_, browser.material_parent);
#if WITH_MODEL_IMPORT
        if (browser.import_requested && !material_create_.active() && !model_import_.request(asset_folder_)) model_error_ = model_import_.error();
        FileDropEvent dropped;
        while (window().take_file_drop(dropped))
        {
            // External file events cannot replace an active modal transaction.
            if (!model_import_.active() && !material_create_.active() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
                browser.accepts_drop(dropped.position) && !model_import_.request(asset_folder_, dropped.paths))
                model_error_ = model_import_.error();
        }
        model_import_.draw(window(), workspace_, selection_, thumbnails_);
#endif
        material_create_.draw(workspace_, selection_, asset_folder_, actor_factory_.default_material()->material()->parameter_schema());
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
        if (!modal_active && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
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
