#include "cube_actor.h"

#include "gamescene/component/scene_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "math/angle.h"
#include "math/quaternion.h"
#include "math/transform.h"

#include <cstddef>
#include <utility>

namespace
{
    toy3d::Quaternion make_axis_rotation(const toy3d::Vector3& axis, float radians)
    {
        toy3d::Quaternion rotation = toy3d::Quaternion::identity();
        if (!toy3d::try_make_quaternion_from_axis_angle(axis, toy3d::Radians(radians), rotation))
        {
            return toy3d::Quaternion::identity();
        }
        return rotation;
    }

    toy3d::Transform make_transform(const toy3d::Vector3& translation, const toy3d::Quaternion& rotation,
                                    float uniform_scale)
    {
        toy3d::Transform transform;
        transform.translation = translation;
        transform.rotation = rotation;
        transform.scale = toy3d::Vector3(uniform_scale);
        return transform;
    }
} // namespace

CubeActor::CubeActor(toy3d::World& world, toy3d::StaticMeshRef mesh) : Actor(world), mesh_(std::move(mesh))
{
    toy3d::SceneComponent& root = create_component<toy3d::SceneComponent>();
    set_root_component(&root);
    root_ = &root;
    root_->set_local_transform(make_transform(toy3d::Vector3(0.0f, 0.0f, 4.0f), toy3d::Quaternion::identity(), 1.0f));

    const std::array<float, 3> offsets = {-1.5f, 0.0f, 1.5f};
    for (std::size_t index = 0; index < cubes_.size(); ++index)
    {
        toy3d::StaticMeshComponent& cube = create_component<toy3d::StaticMeshComponent>();
        cube.attach_to(root_, toy3d::AttachmentRule::KeepRelative);
        cube.set_static_mesh(mesh_);
        cube.set_local_transform(
            make_transform(toy3d::Vector3(offsets[index], 0.0f, 0.0f), toy3d::Quaternion::identity(), 1.0f));
        cubes_[index] = &cube;
    }
    set_tick_enabled(true);
}

void CubeActor::tick(const toy3d::WorldTickContext& context)
{
    const float time = static_cast<float>(context.world_time_seconds);
    if (root_ != nullptr)
    {
        root_->set_local_transform(make_transform(toy3d::Vector3(0.0f, 0.0f, 4.0f),
                                                  make_axis_rotation(toy3d::Vector3(0.0f, 1.0f, 0.0f), time), 1.0f));
    }

    for (std::size_t index = 0; index < cubes_.size(); ++index)
    {
        toy3d::StaticMeshComponent* const cube = cubes_[index];
        if (cube == nullptr)
        {
            continue;
        }
        const float direction = index == 1u ? -1.0f : 1.0f;
        const float angle = time * (1.5f + 0.35f * index) * direction;
        cube->set_local_transform(make_transform(cube->local_transform().translation,
                                                 make_axis_rotation(toy3d::Vector3(1.0f, 0.0f, 0.0f), angle), 1.0f));
    }

    if (cubes_[2] != nullptr)
    {
        cubes_[2]->set_visible(context.frame_number % 48u < 36u);
    }
}
