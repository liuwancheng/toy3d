#pragma once

#include "gamescene/component/actor_component.h"
#include "math/transform.h"

#include <vector>

namespace toy3d
{
    class Actor;
    class World;

    enum class AttachmentRule
    {
        KeepRelative,
        KeepWorld
    };

    class SceneComponent : public ActorComponent
    {
      public:
        explicit SceneComponent(Actor& owner);
        virtual ~SceneComponent();

        SceneComponent(const SceneComponent&) = delete;
        SceneComponent& operator=(const SceneComponent&) = delete;

        SceneComponent* parent() const
        {
            return parent_;
        }
        const std::vector<SceneComponent*>& children() const
        {
            return children_;
        }

        const Transform& local_transform() const
        {
            return local_transform_;
        }
        const Matrix4& world_transform() const
        {
            return world_transform_;
        }
        const Quaternion& world_rotation() const
        {
            return world_rotation_;
        }
        bool set_local_transform(const Transform& transform);
        bool attach_to(SceneComponent* new_parent, AttachmentRule rule);

      protected:
        virtual void on_world_transform_updated()
        {
        }

      private:
        static bool validate_transform(const Transform& transform);
        bool would_create_cycle(const SceneComponent& new_parent) const;
        void update_component_to_world();
        void remove_child(SceneComponent& child);
        SceneComponent* parent_ = nullptr;
        std::vector<SceneComponent*> children_;
        Transform local_transform_;
        Matrix4 world_transform_;
        Quaternion world_rotation_;
    };
} // namespace toy3d
