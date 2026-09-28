#pragma once

#include "gamescene/actor/actor.h"
#include "gamescene/component/light_component.h"

namespace toy3d
{
    class DirectionalLightActor final : public Actor
    {
      public:
        explicit DirectionalLightActor(World& world);
        DirectionalLightComponent& light_component() const { return light_; }
      private:
        DirectionalLightComponent& light_;
    };

    class PointLightActor final : public Actor
    {
      public:
        explicit PointLightActor(World& world);
        PointLightComponent& light_component() const { return light_; }
      private:
        PointLightComponent& light_;
    };
}
