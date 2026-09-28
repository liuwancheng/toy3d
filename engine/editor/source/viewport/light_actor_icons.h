#pragma once

#include "math/matrix4.h"
#include "math/vector2.h"
#include "rendercore/scene/light_scene_proxy.h"

#include <cstdint>

struct ImDrawList;

namespace toy3d
{
    class World;

    // Logical pixels: glyph size is independent of camera distance and Actor scale.
    constexpr float k_light_icon_size = 36.0f;

    struct DirectionalLightArrow
    {
        Vector2 start;
        Vector2 end;
        // Positive reversed-Z change means that the light rays approach the camera.
        float depth_delta = 0.0f;
    };

    bool project_light_direction(const Matrix4& view_projection, const Vector3& position,
                                 const Vector3& direction, const Vector2& image_origin,
                                 const Vector2& image_size, DirectionalLightArrow& arrow);

    bool project_light_icon(const Matrix4& view_projection, const Vector3& position,
                            const Vector2& image_origin, const Vector2& image_size,
                            Vector2& center, float& reversed_depth);
    void draw_light_icon(ImDrawList& draw, LightKind kind, const Vector2& center, float size,
                         bool selected = false, bool hovered = false);
    // Returns the frontmost icon under the mouse. It never mutates World/selection.
    std::uint32_t draw_light_actor_icons(World& world, const Matrix4& view_projection,
                                        const Vector2& image_origin, const Vector2& image_size,
                                        std::uint32_t selected_actor_id, bool allow_hover);
}
