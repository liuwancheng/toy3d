#pragma once

#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"

namespace toy3d
{
    // A placeable camera; projection settings remain reusable on other Actors.
    class CameraActor final : public Actor
    {
      public:
        explicit CameraActor(World& world);
        CameraComponent& camera_component() { return camera_; }
        const CameraComponent& camera_component() const { return camera_; }

      private:
        CameraComponent& camera_;
    };
}
