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
        SceneViewport();
        void begin_frame();
        void draw(World& world, EditorSelection& selection, EditorCommandHistory& history);
        bool extent(Extent& extent) const;
        bool take_hit_request(HitProxyRequest& request);
        void cancel_pending_hit();
        void receive_hit_result(World& world, EditorSelection& selection, const HitProxyResult& result);
        bool view_camera(const World& world, std::uint32_t actor_id);
        void exit_camera_view();
        std::uint32_t viewed_camera_id(const World& world) const;
        void build_scene_views(const World& world, std::vector<SceneView>& views, const Extent& extent) const;

      private:
        SceneView current_view(const World& world, const Extent& extent) const;
        // GT-only observation pose survives camera viewing. World is compared
        // for identity; every camera access resolves an Actor ID in the caller's World.
        Vector3 editor_camera_position_{3.0f, 2.5f, -6.0f};
        Quaternion editor_camera_orientation_;
        const World* camera_world_ = nullptr;
        std::uint32_t camera_actor_id_ = 0;
        Extent scene_extent_;
        Extent previous_scene_extent_;
        HitProxyRequest pending_hit_request_;
        std::uint64_t next_hit_request_id_ = 1;
        std::uint64_t current_hit_request_id_ = 0;
        std::uint64_t viewport_generation_ = 1;
        EditorViewportGizmo gizmo_;
    };
} // namespace toy3d
