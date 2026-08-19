#pragma once

#include "gamescene/actor.h"
#include "rendercore/render_scene_update.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class RenderResourceUpdateCollector;

    class World
    {
    public:
        World();
        ~World() = default;

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        Actor& create_actor();
        bool destroy_actor(Actor& actor);
        void update_transforms();
        RenderSceneUpdateBatch collect_render_scene_updates();

        RenderSceneId render_scene_id() const { return render_scene_id_; }

    private:
        friend class RenderResourceUpdateCollector;

        void collect_actor_removals(const Actor& actor);
        void append_render_resources(RenderResourceUpdateCollector& collector) const;

        std::vector<std::unique_ptr<Actor>> actors_;
        RenderSceneId render_scene_id_;
        std::vector<PrimitiveId> pending_primitive_removals_;
        std::vector<LightId> pending_light_removals_;
    };
}
