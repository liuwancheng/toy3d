#pragma once

#include "gamescene/actor/actor.h"
#include "reflection/reflection_macros.h"

namespace toy3d
{
    TOY3D_REFLECT_TYPE("shadow_demo.RotationSettings", 1)
    struct RotationSettings
    {
        TOY3D_PROPERTY("enabled", Edit)
        bool enabled = true;
        TOY3D_PROPERTY("axis", Edit)
        Vector3 axis = Vector3(0, 1, 0);
        TOY3D_PROPERTY("speed_degrees_per_second", Edit)
        float speed_degrees_per_second = 45.0f;
    };

    class RotatingActor final : public Actor
    {
      public:
        explicit RotatingActor(World& world);
        const RotationSettings& rotation_settings() const { return settings_; }
        bool set_rotation_settings(const RotationSettings& settings);
        static bool valid_settings(const RotationSettings& settings);
      protected:
        void tick(const WorldTickContext& context) override;
      private:
        RotationSettings settings_;
    };
}
