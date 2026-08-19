#include "gamescene/component/scene_component.h"

#include "gamescene/actor.h"
#include "gamescene/world.h"
#include "logging/logger.h"

#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtx/matrix_decompose.hpp"

#include <algorithm>
#include <cmath>

namespace toy3d
{
    namespace
    {
        constexpr float transform_epsilon = 1.0e-5f;

        bool is_finite(const vec3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool is_finite(const quat& value)
        {
            return std::isfinite(value.w) &&
                std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool matrices_nearly_equal(const mat4x4& lhs, const mat4x4& rhs)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (std::abs(lhs[column][row] - rhs[column][row]) > transform_epsilon)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    SceneComponent::SceneComponent(Actor& owner) : owner_(owner) {}

    SceneComponent::~SceneComponent()
    {
        if (parent_ != nullptr)
        {
            parent_->remove_child(*this);
        }

        for (SceneComponent* child : children_)
        {
            child->parent_ = nullptr;
            child->mark_transform_dirty();
        }
    }

    World& SceneComponent::world() const
    {
        return owner_.world();
    }

    bool SceneComponent::validate_transform(const SceneTransform& transform)
    {
        if (!is_finite(transform.translation) ||
            !is_finite(transform.rotation) ||
            !is_finite(transform.scale))
        {
            TOY_LOG_ERROR("Scene transform values must be finite.");
            return false;
        }

        if (transform.scale.x <= EPSILON ||
            transform.scale.y <= EPSILON ||
            transform.scale.z <= EPSILON)
        {
            TOY_LOG_ERROR(
                "Scene transform scale must be positive and greater than EPSILON.");
            return false;
        }

        const float rotation_length = glm::length(transform.rotation);
        if (rotation_length <= EPSILON)
        {
            TOY_LOG_ERROR("Scene transform rotation must be a non-zero quaternion.");
            return false;
        }

        return true;
    }

    bool SceneComponent::decompose_transform(
        const mat4x4& matrix,
        SceneTransform& transform)
    {
        vec3 skew{0.0f};
        vec4 perspective{0.0f};
        SceneTransform result;
        if (!glm::decompose(
            matrix,
            result.scale,
            result.rotation,
            result.translation,
            skew,
            perspective))
        {
            TOY_LOG_ERROR("The requested world transform cannot be decomposed.");
            return false;
        }

        result.rotation = glm::normalize(result.rotation);
        if (!validate_transform(result))
        {
            return false;
        }

        if (glm::length(skew) > transform_epsilon ||
            !matrices_nearly_equal(matrix, make_transform_matrix(result)))
        {
            TOY_LOG_ERROR(
                "KeepWorld would require shear or a transform that cannot be represented as positive-scale TRS.");
            return false;
        }

        transform = result;
        return true;
    }

    bool SceneComponent::set_local_transform(const SceneTransform& transform)
    {
        if (!validate_transform(transform))
        {
            return false;
        }

        local_transform_ = transform;
        local_transform_.rotation = glm::normalize(local_transform_.rotation);
        mark_transform_dirty();
        return true;
    }

    bool SceneComponent::attach_to(
        SceneComponent* new_parent,
        AttachmentRule rule)
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

        SceneTransform new_local_transform = local_transform_;
        if (rule == AttachmentRule::KeepWorld)
        {
            update_world_transform();
            mat4x4 relative_matrix = world_transform_;
            if (new_parent != nullptr)
            {
                new_parent->update_world_transform();
                const float determinant = glm::determinant(new_parent->world_transform_);
                if (!std::isfinite(determinant) || std::abs(determinant) <= EPSILON)
                {
                    TOY_LOG_ERROR(
                        "KeepWorld requires an invertible parent world transform.");
                    return false;
                }
                relative_matrix = glm::inverse(new_parent->world_transform_) * world_transform_;
            }

            if (!decompose_transform(relative_matrix, new_local_transform))
            {
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
        local_transform_ = new_local_transform;
        mark_transform_dirty();
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

    void SceneComponent::mark_transform_dirty()
    {
        transform_dirty_ = true;
        mark_render_dirty(RenderDirtyFlags::Transform);
        for (SceneComponent* child : children_)
        {
            child->mark_transform_dirty();
        }
    }

    void SceneComponent::update_world_transform()
    {
        if (!transform_dirty_)
        {
            return;
        }

        const mat4x4 local_matrix = make_transform_matrix(local_transform_);
        if (parent_ != nullptr)
        {
            parent_->update_world_transform();
            world_transform_ = parent_->world_transform_ * local_matrix;
        }
        else
        {
            world_transform_ = local_matrix;
        }
        transform_dirty_ = false;
    }

    void SceneComponent::remove_child(SceneComponent& child)
    {
        const auto child_iterator = std::find(children_.begin(), children_.end(), &child);
        if (child_iterator != children_.end())
        {
            children_.erase(child_iterator);
        }
    }

    void SceneComponent::mark_render_dirty(RenderDirtyFlags flags)
    {
        render_dirty_flags_ |= flags;
    }

    void SceneComponent::clear_render_dirty()
    {
        render_dirty_flags_ = RenderDirtyFlags::None;
    }
}
