#pragma once

#include "application/application.h"
#include "editor_viewport_gizmo.h"

#include <cstdint>

namespace toy3d
{
    class EditorSelection;
    class EditorCommandHistory;
    class World;

    class SceneViewport
    {
      public:
        void begin_frame();
        void draw(World& world, EditorSelection& selection, EditorCommandHistory& history);
        bool extent(Extent& extent) const;
        bool take_hit_request(HitProxyRequest& request);
        void cancel_pending_hit();
        void receive_hit_result(World& world, EditorSelection& selection, const HitProxyResult& result);
        void build_scene_views(std::vector<SceneView>& views, const Extent& extent) const;

      private:
        Extent scene_extent_;
        Extent previous_scene_extent_;
        HitProxyRequest pending_hit_request_;
        std::uint64_t next_hit_request_id_ = 1;
        std::uint64_t current_hit_request_id_ = 0;
        std::uint64_t viewport_generation_ = 1;
        EditorViewportGizmo gizmo_;
    };
} // namespace toy3d
