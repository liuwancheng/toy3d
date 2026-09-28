#include "viewport/light_actor_icons.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui.h"

#include "gamescene/actor/actor.h"
#include "gamescene/component/light_component.h"
#include "gamescene/world/world.h"

namespace toy3d
{
    namespace
    {
        constexpr int k_sun_ray_count = 8;
        constexpr float k_full_turn = 6.283185307f;
        constexpr float k_direction_preview_length = 2.0f;

        struct LightActorIcon
        {
            std::uint32_t actor_id = 0;
            LightKind kind = LightKind::Directional;
            Vector2 center;
            float depth = 0.0f;
            Vector3 position;
            Vector3 direction;
        };

        void draw_light_direction(ImDrawList& draw, const DirectionalLightArrow& arrow)
        {
            const Vector2 delta = arrow.end - arrow.start;
            const float projected_length = length(delta);
            const ImU32 color = IM_COL32(255, 211, 85, 255);
            const ImU32 outline = IM_COL32(28, 24, 16, 255);
            if (projected_length < 1.0f)
            {
                // A camera-aligned ray has no meaningful 2D arrow direction.
                // Use the familiar dot/cross convention instead of inventing one.
                const ImVec2 cue(arrow.start.x + 32.0f, arrow.start.y + 32.0f);
                draw.AddCircleFilled(cue, 10.0f, outline);
                draw.AddCircle(cue, 8.0f, color, 0, 2.0f);
                if (arrow.depth_delta > 0.0f)
                    draw.AddCircleFilled(cue, 3.0f, color);
                else
                {
                    draw.AddLine(ImVec2(cue.x - 3.0f, cue.y - 3.0f), ImVec2(cue.x + 3.0f, cue.y + 3.0f), color, 2.0f);
                    draw.AddLine(ImVec2(cue.x - 3.0f, cue.y + 3.0f), ImVec2(cue.x + 3.0f, cue.y - 3.0f), color, 2.0f);
                }
                draw.AddText(ImVec2(cue.x + 13.0f, cue.y - ImGui::GetFontSize() * 0.5f), color,
                             arrow.depth_delta > 0.0f ? "Light rays: toward camera" : "Light rays: away from camera");
                return;
            }
            const Vector2 direction = delta / projected_length;
            // Keep the indicator readable at a distance, without changing the
            // direction obtained from the world-space ray's perspective projection.
            const float display_length = (std::max)(80.0f, (std::min)(160.0f, projected_length));
            const Vector2 start = arrow.start + direction * 24.0f;
            const Vector2 tip = arrow.start + direction * display_length;
            const Vector2 base = tip - direction * 12.0f;
            const Vector2 side(-direction.y * 6.0f, direction.x * 6.0f);
            const ImVec2 a(start.x, start.y);
            const ImVec2 b(tip.x, tip.y);
            draw.AddLine(a, b, outline, 5.0f);
            draw.AddLine(a, b, color, 2.5f);
            draw.AddTriangleFilled(b, ImVec2(base.x + side.x, base.y + side.y),
                                   ImVec2(base.x - side.x, base.y - side.y), color);
            draw.AddText(ImVec2(tip.x + 9.0f, tip.y + 5.0f), color, "Light rays");
        }
    }

    bool project_light_direction(const Matrix4& view_projection, const Vector3& position,
                                 const Vector3& direction, const Vector2& image_origin,
                                 const Vector2& image_size, DirectionalLightArrow& arrow)
    {
        DirectionalLightArrow candidate;
        float depth = 0.0f;
        Vector3 normalized_direction;
        if (!project_light_icon(view_projection, position, image_origin, image_size, candidate.start, depth) ||
            !try_normalize(direction, normalized_direction))
            return false;
        const Vector3 end = position + normalized_direction * k_direction_preview_length;
        const Vector4 a = view_projection * Vector4(position.x, position.y, position.z, 1.0f);
        const Vector4 b = view_projection * Vector4(end.x, end.y, end.z, 1.0f);
        if (!is_finite(b)) return false;
        // Clip the world ray against all six homogeneous planes before dividing
        // by W, so an arrow approaching/crossing the camera cannot flip direction.
        const float start_planes[] = {a.w + a.x, a.w - a.x, a.w + a.y, a.w - a.y, a.z, a.w - a.z};
        const float end_planes[] = {b.w + b.x, b.w - b.x, b.w + b.y, b.w - b.y, b.z, b.w - b.z};
        constexpr std::size_t plane_count = sizeof(start_planes) / sizeof(start_planes[0]);
        float end_fraction = 1.0f;
        for (std::size_t plane = 0; plane < plane_count; ++plane)
        {
            if (end_planes[plane] < 0.0f)
            {
                const float intersection = start_planes[plane] / (start_planes[plane] - end_planes[plane]);
                end_fraction = (std::min)(end_fraction, intersection);
            }
        }
        if (end_fraction <= 0.0f) return false;
        const Vector4 clipped = a + (b - a) * end_fraction;
        if (!is_finite(clipped) || clipped.w <= 0.0f) return false;
        candidate.end = Vector2(image_origin.x + (clipped.x / clipped.w + 1.0f) * 0.5f * image_size.x,
                                image_origin.y + (1.0f - clipped.y / clipped.w) * 0.5f * image_size.y);
        candidate.depth_delta = clipped.z / clipped.w - depth;
        if (!is_finite(candidate.end) || !is_finite(candidate.depth_delta)) return false;
        arrow = candidate;
        return true;
    }

    bool project_light_icon(const Matrix4& view_projection, const Vector3& position,
                            const Vector2& image_origin, const Vector2& image_size,
                            Vector2& center, float& reversed_depth)
    {
        if (!is_finite(view_projection) || !is_finite(position) || !is_finite(image_origin) ||
            !is_finite(image_size) || image_size.x <= 0.0f || image_size.y <= 0.0f)
            return false;
        const Vector4 clip = view_projection * Vector4(position.x, position.y, position.z, 1.0f);
        // Clip before division: behind-camera and near/far-plane markers must not
        // produce huge overlay coordinates or accidentally intercept mouse input.
        if (!is_finite(clip) || clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w ||
            clip.x < -clip.w || clip.x > clip.w || clip.y < -clip.w || clip.y > clip.w)
            return false;
        const Vector2 projected(image_origin.x + (clip.x / clip.w + 1.0f) * 0.5f * image_size.x,
                                image_origin.y + (1.0f - clip.y / clip.w) * 0.5f * image_size.y);
        if (!is_finite(projected)) return false;
        center = projected;
        reversed_depth = clip.z / clip.w;
        return true;
    }

    void draw_light_icon(ImDrawList& draw, LightKind kind, const Vector2& center, float size,
                         bool selected, bool hovered)
    {
        if (!is_finite(center) || !is_finite(size) || size <= 0.0f) return;
        const float unit = size / k_light_icon_size;
        const ImVec2 c(center.x, center.y);
        const ImU32 outline = IM_COL32(28, 24, 16, 255);
        const ImU32 gold = IM_COL32(255, 211, 85, 255);
        const ImU32 pale = IM_COL32(255, 242, 182, 255);
        if (selected || hovered)
            draw.AddCircle(c, 20.0f * unit, selected ? IM_COL32(255, 169, 45, 255) : IM_COL32_WHITE,
                           0, 1.5f * unit);
        if (kind == LightKind::Directional)
        {
            for (int ray = 0; ray < k_sun_ray_count; ++ray)
            {
                const float angle = k_full_turn * static_cast<float>(ray) / k_sun_ray_count;
                const float x = std::cos(angle);
                const float y = std::sin(angle);
                const ImVec2 start(c.x + x * 11.0f * unit, c.y + y * 11.0f * unit);
                const ImVec2 end(c.x + x * 16.0f * unit, c.y + y * 16.0f * unit);
                draw.AddLine(start, end, outline, 5.0f * unit);
                draw.AddLine(start, end, gold, 2.5f * unit);
            }
            draw.AddCircleFilled(c, 8.5f * unit, outline);
            draw.AddCircleFilled(c, 6.5f * unit, gold);
            draw.AddCircleFilled(ImVec2(c.x - 2.0f * unit, c.y - 2.0f * unit), 2.0f * unit, pale);
        }
        else if (kind == LightKind::Point)
        {
            // Convex neck plus round glass keeps the bulb readable at small sizes
            // using the existing ImGui version's supported fill primitives.
            const ImVec2 neck[] = {
                ImVec2(c.x - 8.0f * unit, c.y), ImVec2(c.x + 8.0f * unit, c.y),
                ImVec2(c.x + 4.5f * unit, c.y + 7.0f * unit),
                ImVec2(c.x - 4.5f * unit, c.y + 7.0f * unit)};
            constexpr int neck_point_count = static_cast<int>(sizeof(neck) / sizeof(neck[0]));
            draw.AddConvexPolyFilled(neck, neck_point_count, gold);
            draw.AddPolyline(neck, neck_point_count, outline, ImDrawFlags_Closed, 2.0f * unit);
            const ImVec2 glass_center(c.x, c.y - 5.0f * unit);
            draw.AddCircleFilled(glass_center, 10.0f * unit, gold);
            draw.AddCircle(glass_center, 10.0f * unit, outline, 0, 2.0f * unit);
            draw.AddCircleFilled(ImVec2(c.x - 3.0f * unit, c.y - 7.0f * unit), 2.0f * unit, pale);
            draw.AddLine(ImVec2(c.x - 3.0f * unit, c.y + 4.0f * unit),
                         ImVec2(c.x - 3.0f * unit, c.y - 3.0f * unit), outline, 1.2f * unit);
            draw.AddLine(ImVec2(c.x + 3.0f * unit, c.y + 4.0f * unit),
                         ImVec2(c.x + 3.0f * unit, c.y - 3.0f * unit), outline, 1.2f * unit);
            draw.AddLine(ImVec2(c.x - 3.0f * unit, c.y - 3.0f * unit),
                         ImVec2(c.x + 3.0f * unit, c.y - 3.0f * unit), outline, 1.2f * unit);
            draw.AddRectFilled(ImVec2(c.x - 5.0f * unit, c.y + 7.0f * unit),
                               ImVec2(c.x + 5.0f * unit, c.y + 14.0f * unit), outline, 2.0f * unit);
            for (int row = 0; row < 2; ++row)
                draw.AddLine(ImVec2(c.x - 3.0f * unit, c.y + (9.0f + row * 3.0f) * unit),
                             ImVec2(c.x + 3.0f * unit, c.y + (9.0f + row * 3.0f) * unit),
                             IM_COL32(193, 200, 211, 255), 1.5f * unit);
        }
    }

    std::uint32_t draw_light_actor_icons(World& world, const Matrix4& view_projection,
                                        const Vector2& image_origin, const Vector2& image_size,
                                        std::uint32_t selected_actor_id, bool allow_hover)
    {
        std::vector<LightActorIcon> icons;
        for (const auto actor_id : world.actor_ids())
        {
            const Actor* actor = world.find_actor_by_id(actor_id);
            const auto* light = actor ? dynamic_cast<const LightComponent*>(actor->root_component()) : nullptr;
            if (!light) continue;
            LightActorIcon icon;
            icon.actor_id = actor_id;
            if (dynamic_cast<const DirectionalLightComponent*>(light)) icon.kind = LightKind::Directional;
            else if (dynamic_cast<const PointLightComponent*>(light)) icon.kind = LightKind::Point;
            else continue;
            icon.position = transform_position(light->world_transform(), Vector3());
            icon.direction = rotate_vector(light->world_rotation(), Vector3(0, 0, 1));
            if (project_light_icon(view_projection, icon.position,
                                   image_origin, image_size, icon.center, icon.depth))
                icons.push_back(icon);
        }
        // Reversed-Z increases toward the camera: draw far first, hit nearest last.
        std::stable_sort(icons.begin(), icons.end(), [](const LightActorIcon& left, const LightActorIcon& right)
        {
            return left.depth < right.depth;
        });
        const ImVec2 mouse = ImGui::GetMousePos();
        constexpr float hit_radius = k_light_icon_size * 0.5f;
        std::uint32_t hovered_actor_id = 0;
        if (allow_hover)
            for (const LightActorIcon& icon : icons)
                if (std::abs(mouse.x - icon.center.x) <= hit_radius && std::abs(mouse.y - icon.center.y) <= hit_radius)
                    hovered_actor_id = icon.actor_id;
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        draw.PushClipRect(ImVec2(image_origin.x, image_origin.y),
                          ImVec2(image_origin.x + image_size.x, image_origin.y + image_size.y), true);
        for (const LightActorIcon& icon : icons)
        {
            if (icon.kind == LightKind::Directional && icon.actor_id == selected_actor_id)
            {
                DirectionalLightArrow arrow;
                if (project_light_direction(view_projection, icon.position, icon.direction,
                                            image_origin, image_size, arrow))
                    draw_light_direction(draw, arrow);
            }
        }
        for (const LightActorIcon& icon : icons)
            draw_light_icon(draw, icon.kind, icon.center, k_light_icon_size,
                            icon.actor_id == selected_actor_id, icon.actor_id == hovered_actor_id);
        draw.PopClipRect();
        if (hovered_actor_id != 0)
            for (const LightActorIcon& icon : icons)
                if (icon.actor_id == hovered_actor_id)
                    ImGui::SetTooltip("%s (%u)", icon.kind == LightKind::Directional ? "Directional Light" : "Point Light",
                                      icon.actor_id);
        return hovered_actor_id;
    }
}
