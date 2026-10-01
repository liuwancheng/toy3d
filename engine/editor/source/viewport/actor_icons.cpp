#include "viewport/actor_icons.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui.h"

#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/world/world.h"

namespace toy3d
{
    namespace
    {
        constexpr int k_sun_ray_count = 8;
        constexpr float k_full_turn = 6.283185307f;
        constexpr float k_direction_preview_length = 200.0f;

        struct ActorIcon
        {
            std::uint32_t actor_id = 0;
            LightKind kind = LightKind::Directional;
            const CameraComponent* camera = nullptr; // GT-only, consumed during this draw call.
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
            // Projection supplies only direction; a short fixed screen length
            // keeps this indicator independent of the light's camera distance.
            constexpr float display_length = 24.0f;
            const Vector2 start = arrow.start + direction * 24.0f;
            const Vector2 tip = start + direction * display_length;
            const Vector2 base = tip - direction * 8.0f;
            const Vector2 side(-direction.y * 4.0f, direction.x * 4.0f);
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
        if (!project_actor_icon(view_projection, position, image_origin, image_size, candidate.start, depth) ||
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

    bool project_actor_icon(const Matrix4& view_projection, const Vector3& position,
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

    bool project_actor_segment(const Matrix4& view_projection, const Vector3& start, const Vector3& end,
                                const Vector2& image_origin, const Vector2& image_size,
                                Vector2& screen_start, Vector2& screen_end)
    {
        if (!is_finite(view_projection) || !is_finite(start) || !is_finite(end) ||
            !is_finite(image_origin) || !is_finite(image_size) || image_size.x <= 0 || image_size.y <= 0)
            return false;
        const Vector4 a = view_projection * Vector4(start.x, start.y, start.z, 1);
        const Vector4 b = view_projection * Vector4(end.x, end.y, end.z, 1);
        if (!is_finite(a) || !is_finite(b)) return false;
        // This is screen-overlay clipping, not scene visibility. It bounds line
        // coordinates before ImGui tessellation when either endpoint is behind us.
        const float a_planes[] = {a.w + a.x, a.w - a.x, a.w + a.y, a.w - a.y, a.z, a.w - a.z};
        const float b_planes[] = {b.w + b.x, b.w - b.x, b.w + b.y, b.w - b.y, b.z, b.w - b.z};
        constexpr std::size_t plane_count = sizeof(a_planes) / sizeof(a_planes[0]);
        float first = 0.0f;
        float last = 1.0f;
        for (std::size_t i = 0; i < plane_count; ++i)
        {
            if (a_planes[i] < 0 && b_planes[i] < 0) return false;
            if (a_planes[i] < 0 || b_planes[i] < 0)
            {
                const float fraction = a_planes[i] / (a_planes[i] - b_planes[i]);
                if (a_planes[i] < 0) first = (std::max)(first, fraction);
                else last = (std::min)(last, fraction);
            }
        }
        if (first > last) return false;
        const Vector4 clipped_a = a + (b - a) * first;
        const Vector4 clipped_b = a + (b - a) * last;
        if (clipped_a.w <= 0 || clipped_b.w <= 0) return false;
        const Vector2 p(image_origin.x + (clipped_a.x / clipped_a.w + 1) * 0.5f * image_size.x,
                        image_origin.y + (1 - clipped_a.y / clipped_a.w) * 0.5f * image_size.y);
        const Vector2 q(image_origin.x + (clipped_b.x / clipped_b.w + 1) * 0.5f * image_size.x,
                        image_origin.y + (1 - clipped_b.y / clipped_b.w) * 0.5f * image_size.y);
        if (!is_finite(p) || !is_finite(q)) return false;
        screen_start = p;
        screen_end = q;
        return true;
    }

    void draw_camera_icon(ImDrawList& draw, const Vector2& center, float size, bool selected, bool hovered)
    {
        if (!is_finite(center) || !is_finite(size) || size <= 0) return;
        const float unit = size / k_camera_icon_size;
        const ImU32 color = selected ? IM_COL32(255, 169, 45, 255) : IM_COL32(173, 218, 255, 255);
        const ImU32 outline = IM_COL32(22, 29, 38, 255);
        const ImVec2 c(center.x, center.y);
        if (selected || hovered)
            draw.AddCircle(c, 20 * unit, selected ? color : IM_COL32_WHITE, 0, 1.5f * unit);
        draw.AddRectFilled(ImVec2(c.x - 15 * unit, c.y - 10 * unit),
                           ImVec2(c.x + 6 * unit, c.y + 10 * unit), outline, 3 * unit);
        draw.AddRect(ImVec2(c.x - 15 * unit, c.y - 10 * unit),
                     ImVec2(c.x + 6 * unit, c.y + 10 * unit), color, 3 * unit, 0, 2 * unit);
        const ImVec2 lens[] = {ImVec2(c.x + 6 * unit, c.y - 4 * unit),
                              ImVec2(c.x + 15 * unit, c.y - 10 * unit),
                              ImVec2(c.x + 15 * unit, c.y + 10 * unit),
                              ImVec2(c.x + 6 * unit, c.y + 4 * unit)};
        constexpr int lens_count = static_cast<int>(sizeof(lens) / sizeof(lens[0]));
        draw.AddConvexPolyFilled(lens, lens_count, color);
        draw.AddCircle(ImVec2(c.x - 5 * unit, c.y), 4 * unit, color, 0, 1.5f * unit);
    }

    void draw_camera_frustum(ImDrawList& draw, const CameraComponent& camera, float aspect,
                             const Matrix4& view_projection, const Vector2& image_origin,
                             const Vector2& image_size)
    {
        if (!is_finite(aspect) || aspect <= 0) return;
        // A three-metre diagram shows viewing direction and FOV; it does not
        // pretend to draw the camera's potentially kilometre-long clipping range.
        constexpr float preview_length = 300.0f;
        const float half_height = std::tan(to_radians(Degrees(camera.vertical_fov_degrees())).value() * 0.5f) * preview_length;
        const float half_width = half_height * aspect;
        const Vector3 local_corners[] = {{-half_width, -half_height, preview_length},
                                        {half_width, -half_height, preview_length},
                                        {half_width, half_height, preview_length},
                                        {-half_width, half_height, preview_length}};
        constexpr std::size_t corner_count = sizeof(local_corners) / sizeof(local_corners[0]);
        const Vector3 position = transform_position(camera.world_transform(), Vector3());
        const Quaternion& rotation = camera.world_rotation();
        const ImU32 color = IM_COL32(255, 190, 70, 255);
        for (std::size_t i = 0; i < corner_count; ++i)
        {
            const Vector3 corner = position + rotate_vector(rotation, local_corners[i]);
            const Vector3 next = position + rotate_vector(rotation, local_corners[(i + 1) % corner_count]);
            Vector2 a;
            Vector2 b;
            if (project_actor_segment(view_projection, position, corner, image_origin, image_size, a, b))
                draw.AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, 1.5f);
            if (project_actor_segment(view_projection, corner, next, image_origin, image_size, a, b))
                draw.AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, 1.5f);
        }
        const Vector3 forward = rotate_vector(rotation, Vector3(0, 0, 1));
        DirectionalLightArrow arrow;
        if (project_light_direction(view_projection, position, forward, image_origin, image_size, arrow))
        {
            const Vector2 delta = arrow.end - arrow.start;
            if (length(delta) > 1.0f)
            {
                draw.AddLine(ImVec2(arrow.start.x, arrow.start.y), ImVec2(arrow.end.x, arrow.end.y), color, 2);
                draw.AddCircleFilled(ImVec2(arrow.end.x, arrow.end.y), 3, color);
                draw.AddText(ImVec2(arrow.end.x + 6, arrow.end.y), color, "Camera +Z");
            }
        }
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

    std::uint32_t draw_actor_icons(World& world, const Matrix4& view_projection,
                                   const Vector2& image_origin, const Vector2& image_size,
                                   std::uint32_t selected_actor_id, bool allow_hover, float viewport_aspect)
    {
        std::vector<ActorIcon> icons;
        for (const auto actor_id : world.actor_ids())
        {
            const Actor* actor = world.find_actor_by_id(actor_id);
            if (!actor || actor->is_pending_destroy()) continue;
            const auto* light = actor ? dynamic_cast<const LightComponent*>(actor->root_component()) : nullptr;
            const auto* camera = actor ? dynamic_cast<const CameraComponent*>(actor->root_component()) : nullptr;
            if (!light && !camera) continue;
            ActorIcon icon;
            icon.actor_id = actor_id;
            icon.camera = camera;
            if (!camera)
            {
                if (dynamic_cast<const DirectionalLightComponent*>(light)) icon.kind = LightKind::Directional;
                else if (dynamic_cast<const PointLightComponent*>(light)) icon.kind = LightKind::Point;
                else continue;
            }
            const SceneComponent& component = camera ? static_cast<const SceneComponent&>(*camera) : *light;
            icon.position = transform_position(component.world_transform(), Vector3());
            icon.direction = rotate_vector(component.world_rotation(), Vector3(0, 0, 1));
            if (project_actor_icon(view_projection, icon.position,
                                   image_origin, image_size, icon.center, icon.depth))
                icons.push_back(icon);
        }
        // Reversed-Z increases toward the camera: draw far first, hit nearest last.
        std::stable_sort(icons.begin(), icons.end(), [](const ActorIcon& left, const ActorIcon& right)
        {
            return left.depth < right.depth;
        });
        const ImVec2 mouse = ImGui::GetMousePos();
        constexpr float hit_radius = k_light_icon_size * 0.5f;
        std::uint32_t hovered_actor_id = 0;
        if (allow_hover)
            for (const ActorIcon& icon : icons)
                if (std::abs(mouse.x - icon.center.x) <= hit_radius && std::abs(mouse.y - icon.center.y) <= hit_radius)
                    hovered_actor_id = icon.actor_id;
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        draw.PushClipRect(ImVec2(image_origin.x, image_origin.y),
                          ImVec2(image_origin.x + image_size.x, image_origin.y + image_size.y), true);
        // A frustum can intersect the image even when its camera origin is clipped.
        const Actor* selected = world.find_actor_by_id(selected_actor_id);
        const auto* selected_camera = selected ? dynamic_cast<const CameraComponent*>(selected->root_component()) : nullptr;
        if (selected_camera)
            draw_camera_frustum(draw, *selected_camera, viewport_aspect,
                                view_projection, image_origin, image_size);
        for (const ActorIcon& icon : icons)
        {
            if (!icon.camera && icon.kind == LightKind::Directional)
            {
                DirectionalLightArrow arrow;
                if (project_light_direction(view_projection, icon.position, icon.direction,
                                            image_origin, image_size, arrow))
                    draw_light_direction(draw, arrow);
            }
        }
        for (const ActorIcon& icon : icons)
        {
            if (icon.camera)
                draw_camera_icon(draw, icon.center, k_camera_icon_size,
                                 icon.actor_id == selected_actor_id, icon.actor_id == hovered_actor_id);
            else
                draw_light_icon(draw, icon.kind, icon.center, k_light_icon_size,
                                icon.actor_id == selected_actor_id, icon.actor_id == hovered_actor_id);
        }
        draw.PopClipRect();
        if (hovered_actor_id != 0)
            for (const ActorIcon& icon : icons)
                if (icon.actor_id == hovered_actor_id)
                    ImGui::SetTooltip("%s (%u)", icon.camera ? "Camera" :
                                      (icon.kind == LightKind::Directional ? "Directional Light" : "Point Light"),
                                      icon.actor_id);
        return hovered_actor_id;
    }
}
