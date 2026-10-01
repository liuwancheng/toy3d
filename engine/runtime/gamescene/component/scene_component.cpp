#include "gamescene/component/scene_component.h"

#include "logging/logger.h"
#include "gamescene/world/world.h"

#include <algorithm>

namespace toy3d
{
    SceneComponent::SceneComponent(Actor& owner) : ActorComponent(owner)
    {
        update_component_to_world();
    }

    SceneComponent::~SceneComponent()
    {
        if (parent_ != nullptr)
        {
            parent_->remove_child(*this);
        }

        while (!children_.empty())
        {
            SceneComponent* child = children_.back();
            if (!child->attach_to(nullptr, AttachmentRule::KeepWorld))
            {
                child->attach_to(nullptr, AttachmentRule::KeepRelative);
            }
        }
    }

    bool SceneComponent::validate_transform(const Transform& transform)
    {
        if (!is_finite(transform.translation) || !is_finite(transform.rotation) || !is_finite(transform.scale))
        {
            TOY_LOG_ERROR("Scene transform values must be finite.");
            return false;
        }

        if (transform.scale.x <= k_default_float_tolerance || transform.scale.y <= k_default_float_tolerance ||
            transform.scale.z <= k_default_float_tolerance)
        {
            TOY_LOG_ERROR("Scene transform scale must be positive and greater than the default float tolerance.");
            return false;
        }

        Quaternion normalized_rotation;
        if (!try_normalize(transform.rotation, normalized_rotation))
        {
            TOY_LOG_ERROR("Scene transform rotation must be a non-zero quaternion.");
            return false;
        }

        return true;
    }

    bool SceneComponent::set_local_transform(const Transform& transform)
    {
        if (!validate_transform(transform))
        {
            return false;
        }

        Transform candidate = transform;
        candidate.rotation = normalize_unchecked(candidate.rotation);
        if (local_transform_.translation == candidate.translation && local_transform_.rotation == candidate.rotation &&
            local_transform_.scale == candidate.scale) return true;
        local_transform_ = candidate;
        world().mark_content_changed();
        update_component_to_world();
        return true;
    }

    bool SceneComponent::attach_to(SceneComponent* new_parent, AttachmentRule rule)
    {
        if (new_parent == this)
        {
            TOY_LOG_ERROR("A SceneComponent cannot attach to itself.");
            return false;
        }
        if (new_parent != nullptr && &new_parent->world() != &world())
        {
            TOY_LOG_ERROR("Attached SceneComponents must belong to the same World.");
            return false;
        }
        if (new_parent != nullptr && would_create_cycle(*new_parent))
        {
            TOY_LOG_ERROR("The requested SceneComponent attachment would create a cycle.");
            return false;
        }
        if (new_parent == parent_)
        {
            return true;
        }

        Transform new_local_transform = local_transform_;
        if (rule == AttachmentRule::KeepWorld)
        {
            Matrix4 relative_matrix = world_transform_;
            if (new_parent != nullptr)
            {
                Matrix4 inverse_parent;
                if (!try_inverse(new_parent->world_transform_, inverse_parent))
                {
                    TOY_LOG_ERROR("KeepWorld requires an invertible parent world transform.");
                    return false;
                }
                relative_matrix = inverse_parent * world_transform_;
            }

            if (!try_decompose_transform(relative_matrix, new_local_transform))
            {
                TOY_LOG_ERROR(
                    "KeepWorld would require shear or a transform that cannot be represented as positive-scale TRS.");
                return false;
            }
        }

        if (new_parent != nullptr)
        {
            // Grow the new parent's storage before changing either side of the
            // relationship so allocation failure leaves the old hierarchy intact.
            new_parent->children_.push_back(this);
        }
        if (parent_ != nullptr)
        {
            parent_->remove_child(*this);
        }
        parent_ = new_parent;
        world().mark_content_changed();
        local_transform_ = new_local_transform;
        update_component_to_world();
        return true;
    }

    bool SceneComponent::would_create_cycle(const SceneComponent& new_parent) const
    {
        const SceneComponent* ancestor = &new_parent;
        while (ancestor != nullptr)
        {
            if (ancestor == this)
            {
                return true;
            }
            ancestor = ancestor->parent_;
        }
        return false;
    }

    void SceneComponent::update_component_to_world()
    {
        const Matrix4 local_matrix = to_matrix(local_transform_);
        if (parent_ != nullptr)
        {
            world_transform_ = parent_->world_transform_ * local_matrix;
            world_rotation_ = normalize_unchecked(parent_->world_rotation_ * local_transform_.rotation);
        }
        else
        {
            world_transform_ = local_matrix;
            world_rotation_ = local_transform_.rotation;
        }
        on_world_transform_updated();
        for (SceneComponent* child : children_)
        {
            child->update_component_to_world();
        }
    }

    void SceneComponent::remove_child(SceneComponent& child)
    {
        const auto child_iterator = std::find(children_.begin(), children_.end(), &child);
        if (child_iterator != children_.end())
        {
            children_.erase(child_iterator);
        }
    }

} // namespace toy3d
