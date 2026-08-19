#include "gamescene/render_id.h"
#include "gamescene/world.h"

#include "glm/gtc/matrix_transform.hpp"

#include <cmath>
#include <iostream>
#include <type_traits>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }

    bool nearly_equal(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 1.0e-5f;
    }

    bool matrices_nearly_equal(const toy3d::mat4x4& lhs, const toy3d::mat4x4& rhs)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!nearly_equal(lhs[column][row], rhs[column][row]))
                {
                    return false;
                }
            }
        }
        return true;
    }
}

int main()
{
    using namespace toy3d;

    static_assert(!std::is_same<PrimitiveId, LightId>::value,
        "Render IDs from different domains must not be interchangeable");
    check(!PrimitiveId{}, "A default Render ID must be invalid");
    const PrimitiveId primitive_a = allocate_render_id<PrimitiveId>();
    const PrimitiveId primitive_b = allocate_render_id<PrimitiveId>();
    check(primitive_a && primitive_b && primitive_a != primitive_b,
        "Render IDs must be valid and never reused");

    World world;
    Actor& parent_actor = world.create_actor();
    Actor& child_actor = world.create_actor();
    SceneComponent& parent = parent_actor.create_scene_component();
    SceneComponent& child = child_actor.create_scene_component();

    SceneTransform parent_transform;
    parent_transform.translation = vec3(10.0f, 0.0f, 0.0f);
    check(static_cast<bool>(parent.set_local_transform(parent_transform)),
        "A valid positive-scale parent transform must be accepted");

    SceneTransform child_transform;
    child_transform.translation = vec3(0.0f, 2.0f, 0.0f);
    check(static_cast<bool>(child.set_local_transform(child_transform)),
        "A valid child transform must be accepted");
    check(static_cast<bool>(child.attach_to(&parent, AttachmentRule::KeepRelative)),
        "Same-World KeepRelative attachment must succeed");

    world.update_transforms();
    check(nearly_equal(child.world_transform()[3].x, 10.0f) &&
        nearly_equal(child.world_transform()[3].y, 2.0f),
        "Child world transform must equal parent world times local transform");

    parent_transform.translation.x = 20.0f;
    check(static_cast<bool>(parent.set_local_transform(parent_transform)),
        "Updating a parent transform must succeed");
    check(parent.is_transform_dirty() && child.is_transform_dirty(),
        "A parent transform change must dirty all descendants");
    world.update_transforms();
    check(nearly_equal(child.world_transform()[3].x, 20.0f),
        "World transform update must propagate the changed parent transform");

    check(!parent.attach_to(&child, AttachmentRule::KeepRelative),
        "Attachment cycles must fail");

    const mat4x4 child_world_before_detach = child.world_transform();
    check(static_cast<bool>(child.attach_to(nullptr, AttachmentRule::KeepWorld)),
        "KeepWorld detach must succeed for a representable transform");
    world.update_transforms();
    check(matrices_nearly_equal(child_world_before_detach, child.world_transform()),
        "KeepWorld detach must preserve the component world transform");

    check(static_cast<bool>(child.attach_to(&parent, AttachmentRule::KeepWorld)),
        "KeepWorld attachment must succeed for a representable transform");
    world.update_transforms();
    check(matrices_nearly_equal(child_world_before_detach, child.world_transform()),
        "KeepWorld attachment must preserve the component world transform");

    World other_world;
    SceneComponent& other_component =
        other_world.create_actor().create_scene_component();
    check(!child.attach_to(&other_component, AttachmentRule::KeepRelative),
        "Cross-World attachment must be rejected");

    SceneTransform invalid_scale;
    invalid_scale.scale.x = 0.0f;
    check(!child.set_local_transform(invalid_scale),
        "Zero scale must fail");

    check(!static_cast<bool>(parent_actor.set_root_component(&child)),
        "An Actor must reject a root component owned by another Actor");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "GameScene tests passed\n";
    return 0;
}
