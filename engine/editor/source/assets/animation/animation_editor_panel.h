#pragma once

#include "animation/animation_instance.h"
#include "assets/animation/animation_preview_asset.h"
#include "assets/thumbnails/thumbnail_preview_scene.h"
#include "threading/task_graph/task_graph_interface.h"
#include "ui/ui_texture_work.h"

namespace toy3d
{
    class EditorWorkspace;

    // GT-owned read-only asset session. CPU candidates and UI image identities
    // are owned here; the Renderer owns the independent scene and GPU targets.
    class AnimationEditorPanel final
    {
      public:
        explicit AnimationEditorPanel(EditorWorkspace& workspace);
        ~AnimationEditorPanel();
        bool initialize(SceneInterface& scene, MaterialInstanceRef material, TaskGraphInterface& tasks);
        void request_open(const AssetId& id, bool focus = true);
        void invalidate();
        void tick(double delta_seconds);
        void draw();
        void collect_render_work(UiRenderWork& work);
        void on_texture_result(const UiTextureResult& result);
        std::vector<ImGuiTextureId> texture_ids() const;
        const AnimationPreviewAsset* asset() const;
        const std::string& error() const;
        void close();
        void shutdown();
        void seek(double time);
        void set_playing(bool playing);
        void set_preview_display(bool mesh, bool bones, bool depth_test);

      private:
        struct CpuResult;
        bool adopt(std::shared_ptr<const AnimationPreviewAsset> candidate);
        bool prepare_mesh();
        void frame_all();
        void select(const AssetId& mesh, const AssetId& sequence);
        SceneView view() const;
        std::vector<DebugLineVertex> bone_lines(const AnimationEvaluation& evaluation) const;
        void draw_bone(std::uint32_t index);

        EditorWorkspace& workspace_;
        ThumbnailPreviewScene scene_;
        MaterialInstanceRef material_;
        TaskGraphInterface* tasks_ = nullptr;
        GraphEventRef cpu_task_;
        std::shared_ptr<CpuResult> cpu_result_;
        std::shared_ptr<const AnimationPreviewAsset> asset_;
        std::shared_ptr<const AnimationPreviewAsset> previous_asset_;
        SkeletalMeshRef mesh_;
        // One successful CPU snapshot survives closing the preview; no scene/GPU state is cached.
        std::shared_ptr<const AnimationPreviewAsset> cached_asset_;
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
        int tab_ = 0;
    };
} // namespace toy3d
