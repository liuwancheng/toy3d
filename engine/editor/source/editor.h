#pragma once

#include "application/application.h"
#include "editor_viewport_gizmo.h"
#include "rendercore/material/material.h"

#include <cstdint>

namespace toy3d
{
    class StaticMeshActor;

    class EditorApplication final : public Application
    {
      protected:
        bool on_initialize() override;
        void on_shutdown() override;
        void on_build_ui() override;
        bool on_scene_viewport_extent(Extent& extent) const override;
        bool on_hit_proxy_request(HitProxyRequest& request) override;
        void on_hit_proxy_result(const HitProxyResult& result) override;
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override;

      private:
        Extent scene_extent_;
        Extent previous_scene_extent_;
        HitProxyRequest pending_hit_request_;
        std::uint64_t next_hit_request_id_ = 1;
        std::uint64_t current_hit_request_id_ = 0;
        std::uint64_t viewport_generation_ = 1;
        std::uint32_t selected_actor_id_ = 0;
        bool initial_dock_layout_checked_ = false;
        EditorViewportGizmo gizmo_;
        StaticMeshActor* preview_actor_ = nullptr;
        MaterialInstanceRef preview_material_;
    };
} // namespace toy3d
