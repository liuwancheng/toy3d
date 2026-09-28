#include "gamescene/actor/light_actor.h"

#include "logging/logger.h"

namespace toy3d
{
    // --------------------------------------------------------------------------
    // DirectionalLightActor: owns a directional light as its spatial root
    // --------------------------------------------------------------------------
    DirectionalLightActor::DirectionalLightActor(World& world)
        : Actor(world), light_(create_component<DirectionalLightComponent>())
    {
        if (!set_root_component(&light_)) TOY_LOG_ERROR("Directional light root assignment failed.");
    }

    // --------------------------------------------------------------------------
    // PointLightActor: owns a point light as its spatial root
    // --------------------------------------------------------------------------
    PointLightActor::PointLightActor(World& world)
        : Actor(world), light_(create_component<PointLightComponent>())
    {
        if (!set_root_component(&light_)) TOY_LOG_ERROR("Point light root assignment failed.");
    }
}
