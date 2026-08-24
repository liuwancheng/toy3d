#pragma once

#include "gamescene/actor/actor.h"
#include "rendercore/geometry/static_mesh.h"

#include <array>

namespace toy3d
{
    class SceneComponent;
    class StaticMeshComponent;
    class World;
}

class CubeActor final : public toy3d::Actor
{
public:
    CubeActor(toy3d::World& world, toy3d::StaticMeshRef mesh);

private:
    void tick(const toy3d::WorldTickContext& context) override;

    toy3d::StaticMeshRef mesh_;
    toy3d::SceneComponent* root_ = nullptr;
    std::array<toy3d::StaticMeshComponent*, 3> cubes_{};
};
