#pragma once

#include "application/application.h"
#include "editor_viewport_gizmo.h"
#include "placement/asset_placement.h"

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
        bool take_asset_placement(AssetPlacementRequest& request);
        void cancel_pending_hit();
        void receive_hit_result(World& world, EditorSelection& selection, const HitProxyResult& result);
        bool view_camera(const World& world, std::uint32_t actor_id);
        bool focus_actor(const World& world, std::uint32_t actor_id);
        void exit_camera_view();
        std::uint32_t viewed_camera_id(const World& world) const;
        void build_scene_views(const World& world, std::vector<SceneView>& views, const Extent& extent) const;

      private:
        SceneView current_view(const World& world, const Extent& extent) const;
        // GT-only observation pose survives camera viewing. World is compared
        // for identity; every camera access resolves an Actor ID in the caller's World.
        Vector3 editor_camera_position_{300.0f, 250.0f, -600.0f};
        Vector3 editor_camera_target_{0.0f, 0.0f, 300.0f};
        Quaternion editor_camera_orientation_;
        float editor_camera_yaw_ = 0.0f;
        float editor_camera_pitch_ = 0.0f;
        bool orbit_drag_active_ = false;
        bool pan_drag_active_ = false;
        const World* camera_world_ = nullptr;
        std::uint32_t camera_actor_id_ = 0;
        Extent scene_extent_;
        Extent previous_scene_extent_;
        HitProxyRequest pending_hit_request_;
        std::uint64_t next_hit_request_id_ = 1;
        std::uint64_t current_hit_request_id_ = 0;
        std::uint64_t viewport_generation_ = 1;
        EditorViewportGizmo gizmo_;
        AssetPlacementRequest asset_placement_;
        bool asset_placement_pending_ = false;
    };
} // namespace toy3d
