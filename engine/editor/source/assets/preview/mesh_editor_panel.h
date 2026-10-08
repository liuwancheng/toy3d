#pragma once

#include "animation/animation_instance.h"
#include "asset_loader/asset_loader.h"
#include "assets/preview/mesh_preview_asset.h"
#include "assets/preview/asset_preview_scene.h"
#include "assets/mesh/mesh_material_edit_session.h"
#include "threading/task_graph/task_graph_interface.h"
#include "ui/ui_texture_work.h"

namespace toy3d
{
    class EditorWorkspace;
    class AssetResourcePicker;

    // GT-owned mesh asset session. CPU candidates and UI image identities
    // are owned here; the Renderer owns the independent scene and GPU targets.
    class MeshEditorPanel final
    {
      public:
        explicit MeshEditorPanel(EditorWorkspace& workspace);
        ~MeshEditorPanel();
        bool initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks);
        // GT composition root injects the shared asset loader before initialize().
        void set_asset_loader(AssetLoader& assets)
        {
            assets_ = &assets;
        }
        void set_resource_picker(AssetResourcePicker& picker)
        {
            resource_picker_ = &picker;
        }
        void set_material_resolver(MeshMaterialResolver resolver)
        {
            material_resolver_ = std::move(resolver);
            scene_.set_material_resolver(material_resolver_);
        }
        MeshMaterialEditSession& material_edit_session()
        {
            return material_session_;
        }
        AssetStatus assign_material(std::size_t slot, const AssetRef& material);
        AssetStatus undo_material();
        AssetStatus redo_material();
        AssetStatus save_materials();
        bool request_exit();
        bool take_exit();
        bool modal_pending() const
        {
            return pending_close_ || pending_open_id_.valid();
        }
        void request_open(const AssetId& id, bool focus = true);
        void invalidate();
        void tick(double delta_seconds);
        void draw();
        void collect_render_work(UiRenderWork& work);
        void on_texture_result(const UiTextureResult& result);
        std::vector<ImGuiTextureId> texture_ids() const;
        const MeshPreviewAsset* asset() const;
        const std::string& error() const;
        void close();
        void shutdown();
        void seek(double time);
        void set_playing(bool playing);
        void set_preview_display(bool mesh, bool bones, bool depth_test);
        bool set_preview_scene_settings(const PreviewSceneSettings& settings);
        const PreviewSceneSettings& preview_scene_settings() const;
        void set_preview_mesh_changed(std::function<void()> changed)
        {
            preview_mesh_changed_ = std::move(changed);
        }

      private:
        struct CpuResult;
        bool adopt(std::shared_ptr<const MeshPreviewAsset> candidate);
        bool prepare_mesh();
        void frame_all();
        void select(const AssetId& mesh, const AssetId& sequence);
        SceneView view() const;
        std::vector<DebugLineVertex> bone_lines(const AnimationEvaluation& evaluation) const;
        void draw_bone(std::uint32_t index);
        void draw_asset_details();
        AssetStatus prepare_materials(const std::vector<AssetRef>& materials);
        void draw_materials();
        void draw_unsaved_prompt();
        void draw_bone_details();
        void draw_preview_selectors();
        void draw_animation_browser();
        void draw_playback();
        void draw_viewport();

        EditorWorkspace& workspace_;
        MeshMaterialEditSession material_session_;
        MeshMaterialResolver material_resolver_;
        std::vector<MaterialInterfaceRef> preview_materials_;
        AssetId pending_open_id_;
        bool pending_close_ = false;
        bool pending_exit_ = false;
        bool exit_ready_ = false;
        bool close_next_frame_ = false;
        AssetResourcePicker* resource_picker_ = nullptr;
        AssetPreviewScene scene_;
        PreviewSceneSettings preview_settings_;
        AssetLoader* assets_ = nullptr;
        TextureRef environment_cube_;
        AssetId loaded_environment_;
        std::vector<AssetId> compatible_sequences_;
        MaterialInstanceRef material_;
        TaskGraphInterface* tasks_ = nullptr;
        GraphEventRef cpu_task_;
        std::shared_ptr<CpuResult> cpu_result_;
        std::shared_ptr<const MeshPreviewAsset> asset_;
        std::shared_ptr<const MeshPreviewAsset> previous_asset_;
        SkeletalMeshRef mesh_;
        // One successful CPU snapshot survives closing the preview; no scene/GPU state is cached.
        std::shared_ptr<const MeshPreviewAsset> cached_asset_;
        SkeletalMeshRef cached_mesh_;
        AnimationInstance animation_;
        AnimationPlaybackSettings playback_;
        AssetId requested_id_;
        AssetId selected_mesh_;
        AssetId selected_sequence_;
        UiRenderWork pending_work_;
        ImGuiTextureId texture_id_;
        ImGuiTextureId candidate_id_;
        std::uint64_t revision_ = 0;
        std::uint64_t candidate_revision_ = 0;
        std::uint64_t preview_revision_ = 0;
        std::uint64_t candidate_preview_revision_ = 0;
        std::uint64_t next_request_ = 1;
        std::uint64_t next_texture_ = 1ull << 44;
        std::int32_t selected_bone_ = -1;
        Vector3 center_;
        float radius_ = 100;
        float distance_ = 400;
        float yaw_ = 25;
        float pitch_ = 12;
        double loading_seconds_ = 0;
        Extent extent_{512, 384};
        std::string error_;
        bool open_ = false;
        bool visible_ = false;
        bool focus_requested_ = false;
        bool needs_load_ = false;
        bool override_selection_ = false;
        bool render_dirty_ = false;
        bool mesh_dirty_ = false;
        bool show_mesh_ = true;
        bool show_bones_ = true;
        bool depth_test_ = false;
        bool lock_root_ = false;
        bool initialized_ = false;
        bool mesh_preference_pending_ = false;
        std::function<void()> preview_mesh_changed_;
    };
} // namespace toy3d
