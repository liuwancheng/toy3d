#include "placement/actor_placement.h"

#include <cmath>

namespace toy3d
{
    bool calculate_placement_transform(const Matrix4& view, const Matrix4& projection,
                                       const Vector3& camera_position, const Vector2& image_position,
                                       const PlacementItem& item, Transform& result, bool* on_ground)
    {
        if (!is_finite(camera_position) || !is_finite(image_position) || image_position.x < 0 ||
            image_position.y < 0 || image_position.x > 1 || image_position.y > 1) return false;
        Matrix4 inverse;
        if (!try_inverse(projection * view, inverse)) return false;
        const Vector4 far_clip(image_position.x * 2 - 1, 1 - image_position.y * 2, 0, 1);
        const Vector4 far_world = inverse * far_clip;
        if (!is_finite(far_world) || std::abs(far_world.w) < 1.0e-6f) return false;
        Vector3 direction;
        if (!try_normalize(Vector3(far_world.x, far_world.y, far_world.z) / far_world.w - camera_position,
                           direction)) return false;
        constexpr float default_distance = 8.0f;
        constexpr float maximum_ground_distance = 100.0f;
        float distance = default_distance;
        bool ground = false;
        if (std::abs(direction.y) > 1.0e-5f)
        {
            const float intersection = -camera_position.y / direction.y;
            ground = intersection > 0 && intersection <= maximum_ground_distance;
            if (ground) distance = intersection;
        }
        Transform placed;
        placed.translation = camera_position + direction * distance;
        if (ground) placed.translation.y += item.ground_offset;
        if (item.id == PlacementItemId::DirectionalLight &&
            !try_make_rotation_from_forward_up(Vector3(-0.35f, -0.55f, 0.75f), Vector3(0, 1, 0), placed.rotation))
            return false;
        result = placed;
        if (on_ground) *on_ground = ground;
        return true;
    }
}
