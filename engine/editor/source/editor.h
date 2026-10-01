#pragma once

#include "application/application.h"
#include "scene/editor_scene_session.h"
#include "scene/placement/actor_factory.h"
#include "rendercore/material/material.h"
#include "scene/editor_selection.h"
#include "viewport/scene_viewport.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "assets/mesh/static_mesh_import_dialog.h"
#include "assets/texture/texture_import_dialog.h"
#include "assets/material/material_create_dialog.h"
#include "assets/material/material_editor_panel.h"
#include "panels/editor_panel_registry.h"
#include "panels/content_browser_panel.h"
#include "panels/console_panel.h"
#include "panels/editor_notifications.h"
#include "shader/shader_create_dialog.h"
#include "panels/place_actors_panel.h"
#include "assets/asset_editor_registry.h"
#include "assets/texture/texture_preview_panel.h"
#include "shader/shader_workflow.h"
#include "asset/scene/scene_asset.h"

#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    class EditorWorkspace;

    class EditorApplication final : public Application
    {
      public:
        explicit EditorApplication(EditorWorkspace& workspace, std::shared_ptr<LogBuffer> log_buffer = {})
            : workspace_(workspace), scene_session_(workspace_, actor_factory_, material_assignments_, selection_, scene_viewport_),
              thumbnails_(workspace), texture_preview_(workspace), console_(log_buffer), notifications_(std::move(log_buffer)) {}

      protected:
        bool on_initialize() override;
        bool starts_world_play() const override { return false; }
        void on_shutdown() override;
        bool on_close_requested() override;
        void on_build_ui() override;
        bool on_scene_viewport_extent(Extent& extent) const override;
        bool on_hit_proxy_request(HitProxyRequest& request) override;
        void on_hit_proxy_result(const HitProxyResult& result) override;
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override;
        bool uses_preview_scene() const override { return true; }
        bool on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override;
        void on_tick(double) override;
        void on_collect_material_validation(std::vector<MaterialProgramValidationRef>& requests) override { if (shader_workflow_ready_) shaders_.collect_validation(requests); }
        void on_collect_builtin_shader_updates(std::vector<BuiltinShaderUpdateRef>& requests) override { if (shader_workflow_ready_) shaders_.collect_builtin_updates(requests); }
        void on_collect_ui_render_work(UiRenderWork& work) override
        { thumbnails_.collect_render_work(work); texture_preview_.collect_render_work(work); }
        void on_ui_texture_result(UiTextureResult result) override
        { texture_preview_.on_texture_result(result); thumbnails_.on_texture_result(std::move(result)); }
        std::vector<ImGuiTextureId> ui_texture_ids() const override
        { auto ids = thumbnails_.texture_ids(); const auto preview = texture_preview_.texture_ids();
          ids.insert(ids.end(), preview.begin(), preview.end()); return ids; }

      private:
        EditorWorkspace& workspace_;
        EditorSelection selection_;
        ActorFactory actor_factory_;
        std::unique_ptr<MaterialLibrary> materials_;
        MaterialAssignments material_assignments_;
        SceneViewport scene_viewport_;
        EditorSceneSession scene_session_;
        AssetThumbnailPool thumbnails_;
        TexturePreviewPanel texture_preview_;
        bool initial_dock_layout_checked_ = false;
        bool reset_dock_layout_ = false;
        std::string asset_folder_ = "/Project";
        bool show_engine_content_ = false;
        StaticMeshImportDialog model_import_;
        TextureImportDialog texture_import_;
        MaterialCreateDialog material_create_;
        MaterialEditorPanel material_editor_;
        NativeProcessService processes_;
        ThreadManager shader_threads_;
        ShaderWorkflow shaders_{processes_, shader_threads_};
        bool shader_workflow_ready_ = false;
        EditorPanelRegistry panels_;
        ConsolePanel console_;
        EditorNotifications notifications_;
        ShaderCreateDialog shader_create_;
        PlaceActorsPanel place_actors_;
        ContentBrowserPanel content_browser_;
        AssetEditorRegistry asset_editors_;
        bool register_panels();
        void draw_main_menu();
        void draw_asset_browser();
        void undo_edit();
        void redo_edit();
        void apply_scene_history(bool redo);
        std::string model_error_;
        std::string material_assignment_error_;
        enum class SceneAction { None, New, Open, Exit };
        SceneAction pending_scene_action_ = SceneAction::None;
        AssetId pending_scene_id_;
        bool show_scene_save_as_ = false;
        bool scene_confirm_requested_ = false;
        bool discard_scene_on_exit_ = false;
        char scene_name_[256] = "NewScene";
        std::string scene_error_;

        bool save_scene(const VirtualPath& path, bool create_new);
        bool open_scene(const AssetId& id);
        void new_scene();
        void request_scene_action(SceneAction action, const AssetId& id = {});
        void draw_scene_dialogs();
        bool scene_dirty() const;

    };
} // namespace toy3d
