#include "gamescene/actor/camera_actor.h"

#include "logging/logger.h"

namespace toy3d
{
    CameraActor::CameraActor(World& world)
        : Actor(world), camera_(create_component<CameraComponent>())
    {
        if (!set_root_component(&camera_))
            TOY_LOG_ERROR("Camera root assignment failed.");
    }
}
