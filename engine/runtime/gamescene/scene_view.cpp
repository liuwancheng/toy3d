#include "gamescene/scene_view.h"

#include "gamescene/world/world.h"
#include "gamescene/component/camera_component.h"
#include "math/matrix_construction.h"

namespace toy3d
{
    void build_game_scene_views(const World& world, std::vector<SceneView>& views, const Extent& extent)
    {
        for (const auto id : world.actor_ids())
        {
            const Actor* actor = world.find_actor_by_id(id);
            for (const auto component_id : actor->component_ids())
            {
                const auto* camera = dynamic_cast<const CameraComponent*>(actor->find_component_by_id(component_id));
                if (!camera)
                {
                    continue;
                }
                const auto& settings = camera->camera_settings();
                const Vector3 position = transform_position(camera->world_transform(), Vector3());
                const auto rotation = camera->world_rotation();
                views.emplace_back(position, rotation, rotate_vector(rotation, Vector3(0, 0, 1)),
                                   IntRect{0, 0, extent.width, extent.height}, extent, camera->projection_mode(),
                                   to_radians(Degrees(settings.vertical_fov)), settings.near_clip, settings.far_clip);
                return;
            }
        }
        const Vector3 position(650, 450, -900);
        const Vector3 direction = normalized_or_zero(Vector3(0, 100, 0) - position);
        Quaternion rotation;
        if (try_make_rotation_from_forward_up(direction, Vector3(0, 1, 0), rotation))
        {
            views.emplace_back(position, rotation, direction, IntRect{0, 0, extent.width, extent.height}, extent,
                               CameraProjectionMode::Perspective, to_radians(Degrees(60)), 10.0f, 100000.0f);
        }
    }
} // namespace toy3d
