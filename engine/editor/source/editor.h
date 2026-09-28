#pragma once

#include "application/application.h"
#include "commands/editor_command_history.h"
#include "placement/actor_factory.h"
#include "rendercore/material/material.h"
#include "selection/editor_selection.h"
#include "viewport/scene_viewport.h"

#include <string>
#include <array>

namespace toy3d
{
    class StaticMeshActor;
    class EditorWorkspace;

    class EditorApplication final : public Application
    {
      public:
        explicit EditorApplication(EditorWorkspace& workspace) : workspace_(workspace), command_history_(actor_factory_) {}

      protected:
        bool on_initialize() override;
        bool starts_world_play() const override { return false; }
        void on_shutdown() override;
        void on_build_ui() override;
        bool on_scene_viewport_extent(Extent& extent) const override;
        bool on_hit_proxy_request(HitProxyRequest& request) override;
        void on_hit_proxy_result(const HitProxyResult& result) override;
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override;

      private:
        EditorWorkspace& workspace_;
        EditorSelection selection_;
        ActorFactory actor_factory_;
        EditorCommandHistory command_history_;
        SceneViewport scene_viewport_;
        bool initial_dock_layout_checked_ = false;
        bool reset_dock_layout_ = false;
        std::string asset_folder_ = "/Project";
        bool show_engine_content_ = false;
        void draw_model_import_dialog();
        void place_selected_static_mesh();
        std::array<char, 1024> import_source_{};
        std::array<char, 256> import_asset_name_{};
        std::string import_folder_;
        std::string model_error_;
        float import_scale_ = 1.0f;
        bool open_import_dialog_ = false;

    };
} // namespace toy3d
