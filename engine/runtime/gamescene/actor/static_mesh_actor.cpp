#include "gamescene/actor/static_mesh_actor.h"

namespace toy3d
{
    StaticMeshActor::StaticMeshActor(World& world) : Actor(world)
    {
        static_mesh_component_ = &create_component<StaticMeshComponent>();
        set_root_component(static_mesh_component_);
    }
} // namespace toy3d
