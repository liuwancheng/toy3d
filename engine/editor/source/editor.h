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
#include "material/material_shader_workflow.h"

#include <string>
#include <utility>

namespace toy3d
{
    class StaticMeshActor;
    class EditorWorkspace;

    class EditorApplication final : public Application
    {
      public:
        explicit EditorApplication(EditorWorkspace& workspace) : workspace_(workspace), command_history_(actor_factory_, material_assignments_), thumbnails_(workspace) {}

      protected:
        bool on_initialize() override;
        bool starts_world_play() const override { return false; }
        void on_shutdown() override;
        bool on_close_requested() override { return material_editor_.request_exit(); }
        void on_build_ui() override;
        bool on_scene_viewport_extent(Extent& extent) const override;
        bool on_hit_proxy_request(HitProxyRequest& request) override;
        void on_hit_proxy_result(const HitProxyResult& result) override;
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override;
        bool uses_preview_scene() const override { return true; }
        bool on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override;
        void on_tick(double) override;
        void on_collect_material_validation(std::vector<MaterialProgramValidationRef>& requests) override { if (shader_workflow_ready_) shaders_.collect_validation(requests); }
        void on_collect_ui_render_work(UiRenderWork& work) override { thumbnails_.collect_render_work(work); }
        void on_ui_texture_result(UiTextureResult result) override { thumbnails_.on_texture_result(std::move(result)); }
        std::vector<ImGuiTextureId> ui_texture_ids() const override { return thumbnails_.texture_ids(); }

      private:
        EditorWorkspace& workspace_;
        EditorSelection selection_;
        ActorFactory actor_factory_;
        std::unique_ptr<MaterialLibrary> materials_;
        MaterialAssignments material_assignments_;
        EditorCommandHistory command_history_;
        SceneViewport scene_viewport_;
        AssetThumbnailPool thumbnails_;
        float asset_tile_size_ = 112.0f;
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

    };
} // namespace toy3d
