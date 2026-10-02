#include "gamescene/actor/skeletal_mesh_actor.h"

namespace toy3d
{
    SkeletalMeshActor::SkeletalMeshActor(World& world) : Actor(world)
    {
        mesh_component_ = &create_component<SkeletalMeshComponent>();
        set_root_component(mesh_component_);
    }

    SkeletalMeshComponent& SkeletalMeshActor::skeletal_mesh_component()
    {
        return *mesh_component_;
    }

    const SkeletalMeshComponent& SkeletalMeshActor::skeletal_mesh_component() const
    {
        return *mesh_component_;
    }
} // namespace toy3d
