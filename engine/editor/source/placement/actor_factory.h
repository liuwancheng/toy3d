#pragma once

#include "placement/placement_catalog.h"
#include "rendercore/geometry/static_mesh.h"

#include <map>

namespace toy3d
{
    class Actor;
    class World;

    struct EditorActorState
    {
        Transform transform;
        bool primitive_cast_shadows = true;
        bool primitive_receives_shadows = true;
        bool light_enabled = true;
        Vector3 light_color{1.0f};
        float light_intensity = 1.0f;
        float light_range = 10.0f;
        int light_priority = 0;
        bool shadow_cast_shadows = false;
        float shadow_distance = 100.0f;
        float shadow_distance_fade_fraction = 0.1f;
        float shadow_bias = 0.5f;
        float shadow_slope_bias = 0.5f;
        float camera_vertical_fov = 60.0f;
        float camera_near_clip = 0.1f;
        float camera_far_clip = 1000.0f;
    };

    EditorActorState capture_actor_state(const Actor& actor);
    bool apply_actor_state(Actor& actor, const EditorActorState& state);

    // Holds geometry across add/remove commands; release only after scene drain.
    class ActorFactory
    {
      public:
        bool initialize();
        void release();
        Actor* create(World& world, const PlacementRequest& request);
        bool describe(const Actor& actor, PlacementRequest& request) const;
        const char* label(std::uint32_t actor_id) const;
        void forget(std::uint32_t actor_id);
        const MaterialInstanceRef& default_material() const { return material_; }
      private:
        StaticMeshRef cube_;
        StaticMeshRef plane_;
        MaterialInstanceRef material_;
        std::map<std::uint32_t, PlacementRequest> placed_items_;
    };
}
