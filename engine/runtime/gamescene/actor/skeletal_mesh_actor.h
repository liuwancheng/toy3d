#pragma once

#include "gamescene/actor/actor.h"
#include "gamescene/component/skeletal_mesh_component.h"

namespace toy3d
{
    class SkeletalMeshActor final : public Actor
    {
      public:
        explicit SkeletalMeshActor(World& world);
        SkeletalMeshComponent& skeletal_mesh_component();
        const SkeletalMeshComponent& skeletal_mesh_component() const;

      private:
        SkeletalMeshComponent* mesh_component_ = nullptr;
    };
} // namespace toy3d
