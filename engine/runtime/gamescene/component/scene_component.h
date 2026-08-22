#pragma once

#include "math/transform.h"
#include "rendercore/render_dirty.h"

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

    class SceneComponent
    {
    public:
        explicit SceneComponent(Actor& owner);
        virtual ~SceneComponent();

        SceneComponent(const SceneComponent&) = delete;
        SceneComponent& operator=(const SceneComponent&) = delete;

        Actor& owner() const { return owner_; }
        World& world() const;
        SceneComponent* parent() const { return parent_; }
        const std::vector<SceneComponent*>& children() const { return children_; }

        const Transform& local_transform() const { return local_transform_; }
        const Matrix4& world_transform() const { return world_transform_; }
        const Quaternion& world_rotation() const { return world_rotation_; }
        bool is_transform_dirty() const { return transform_dirty_; }

        bool set_local_transform(const Transform& transform);
        bool attach_to(SceneComponent* new_parent, AttachmentRule rule);

    private:
        friend class World;

        static bool validate_transform(const Transform& transform);
        bool would_create_cycle(const SceneComponent& new_parent) const;
        void mark_transform_dirty();
        void update_world_transform();
        void remove_child(SceneComponent& child);

    protected:
        void mark_render_dirty(RenderDirtyFlags flags);
        RenderDirtyFlags render_dirty_flags() const { return render_dirty_flags_; }
        void clear_render_dirty();

        Actor& owner_;
        SceneComponent* parent_ = nullptr;
        std::vector<SceneComponent*> children_;
        Transform local_transform_;
        Matrix4 world_transform_;
        Quaternion world_rotation_;
        bool transform_dirty_ = true;
        RenderDirtyFlags render_dirty_flags_ = RenderDirtyFlags::None;
    };
}
