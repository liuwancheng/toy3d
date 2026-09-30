#pragma once

#include "application/application.h"
#include "commands/editor_command_history.h"
#include "placement/actor_factory.h"
#include "rendercore/material/material.h"
#include "selection/editor_selection.h"
#include "viewport/scene_viewport.h"
#include "thumbnails/asset_thumbnail_pool.h"
#include "panels/static_mesh_import_dialog.h"
#include "panels/texture_import_dialog.h"
#include "panels/material_create_dialog.h"
#include "panels/material_editor_panel.h"
#include "panels/texture_preview_panel.h"
#include "material/material_shader_workflow.h"
#include "scene_asset/scene_asset.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace toy3d
{
    class StaticMeshActor;
    class EditorWorkspace;

    class EditorApplication final : public Application
    {
      public:
        explicit EditorApplication(EditorWorkspace& workspace) : workspace_(workspace), command_history_(actor_factory_, material_assignments_), thumbnails_(workspace), texture_preview_(workspace) {}

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
        EditorCommandHistory command_history_;
        SceneViewport scene_viewport_;
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
        MaterialShaderWorkflow shaders_{processes_, shader_threads_};
        bool shader_workflow_ready_ = false;
        bool material_history_target_ = false;
        void undo_edit();
        void redo_edit();
        std::string model_error_;
        std::string material_assignment_error_;
        enum class SceneAction { None, New, Open, Exit };
        SceneAction pending_scene_action_ = SceneAction::None;
        AssetId pending_scene_id_;
        AssetId scene_id_;
        VirtualPath scene_path_;
        std::map<std::uint32_t, std::pair<std::string, std::string>> stable_scene_ids_;
        std::vector<std::uint8_t> scene_baseline_;
        bool show_scene_save_as_ = false;
        bool scene_confirm_requested_ = false;
        bool discard_scene_on_exit_ = false;
        char scene_name_[256] = "NewScene";
        std::string scene_error_;

        bool capture_scene(SceneAssetData& data, std::string& error);
        bool replace_scene(const SceneAssetData& data, std::string& error);
        bool save_scene(const VirtualPath& path, bool create_new);
        bool open_scene(const AssetId& id);
        void new_scene();
        void request_scene_action(SceneAction action, const AssetId& id = {});
        void draw_scene_dialogs();
        bool scene_dirty();

    };
} // namespace toy3d
