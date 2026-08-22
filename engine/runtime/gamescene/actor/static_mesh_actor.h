#pragma once

#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"

namespace toy3d
{
    class StaticMeshActor final : public Actor
    {
    public:
        explicit StaticMeshActor(World& world);
        ~StaticMeshActor() override = default;

        StaticMeshComponent& static_mesh_component()
        {
            return *static_mesh_component_;
        }
        const StaticMeshComponent& static_mesh_component() const
        {
            return *static_mesh_component_;
        }

    private:
        StaticMeshComponent* static_mesh_component_ = nullptr;
    };
}
