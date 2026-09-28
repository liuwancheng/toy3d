#include "viewport/scene_viewport.h"

#include "imgui.h"
#include "ImGuizmo.h"

#include "commands/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "selection/editor_selection.h"
#include "placement/actor_placement.h"
#include "ui/imgui_draw_data.h"
#include "viewport/light_actor_icons.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace toy3d
{
    namespace
    {
        constexpr Vector3 k_camera_position(3.0f, 2.5f, -6.0f);
        constexpr Vector3 k_camera_target(0.0f, 0.0f, 3.0f);
        constexpr float k_near_clip = 0.1f;
        constexpr float k_far_clip = 1000.0f;

        bool make_camera(Quaternion& orientation, Vector3& direction)
        {
            return try_normalize(k_camera_target - k_camera_position, direction) &&
                   try_make_rotation_from_forward_up(direction, Vector3(0.0f, 1.0f, 0.0f), orientation);
        }

        std::uint32_t physical_extent(float logical, float scale)
        {
            const double value = static_cast<double>(logical) * static_cast<double>(scale);
            if (!std::isfinite(value) || value <= 0.0 ||
                value > static_cast<double>((std::numeric_limits<std::uint32_t>::max)()))
                return 0u;
            return static_cast<std::uint32_t>(std::floor(value + 0.5));
        }

        std::uint32_t physical_pixel(float logical, float scale)
        {
            const double value = static_cast<double>(logical) * static_cast<double>(scale);
            if (!std::isfinite(value) || value < 0.0 ||
                value > static_cast<double>((std::numeric_limits<std::uint32_t>::max)()))
                return (std::numeric_limits<std::uint32_t>::max)();
            return static_cast<std::uint32_t>(std::floor(value));
        }
    } // namespace

    void SceneViewport::begin_frame()
    {
        gizmo_.begin_frame();
        pending_hit_request_ = {};
        scene_extent_ = {};
    }

    void SceneViewport::draw(World& world, EditorSelection& selection, EditorCommandHistory& history)
    {
        // Keep the persisted ImGui window identity while changing its visible title.
        const bool visible = ImGui::Begin("Scene Viewport###Game Viewport");
        if (visible)
        {
            gizmo_.draw_toolbar();
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            scene_extent_.width = physical_extent(available.x, scale.x);
            scene_extent_.height = physical_extent(available.y, scale.y);
            if (scene_extent_.width != 0u && scene_extent_.height != 0u)
            {
                const std::uintptr_t id = static_cast<std::uintptr_t>(IMGUI_SCENE_VIEWPORT_TEXTURE_ID.value());
                ImGui::Image(reinterpret_cast<ImTextureID>(id), available);
                const bool hovered = ImGui::IsItemHovered();
                const ImVec2 origin = ImGui::GetItemRectMin();
                gizmo_.handle_shortcuts(hovered);
                bool placement_active = ImGui::GetDragDropPayload() != nullptr;
                if (ImGui::BeginDragDropTarget())
                {
                    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        PLACEMENT_DRAG_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery);
                    if (payload && payload->DataSize == sizeof(PlacementItemId))
                    {
                        // Copy a small value identity; the payload never owns World objects.
                        const PlacementItemId item_id = *static_cast<const PlacementItemId*>(payload->Data);
                        const PlacementItem* item = find_placement_item(item_id);
                        Quaternion orientation;
                        Vector3 direction;
                        Matrix4 view;
                        Matrix4 projection;
                        PerspectiveProjectionDesc desc;
                        desc.vertical_fov = to_radians(Degrees(60));
                        desc.aspect = available.x / available.y;
                        desc.near_clip = k_near_clip;
                        desc.far_clip = k_far_clip;
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const Vector2 position((mouse.x - origin.x) / available.x, (mouse.y - origin.y) / available.y);
                        PlacementRequest request;
                        request.item = item_id;
                        if (item && make_camera(orientation, direction) &&
                            try_make_view_matrix(k_camera_position, orientation, view) &&
                            try_make_perspective_projection(desc, projection) &&
                            calculate_placement_transform(view, projection, k_camera_position, position, *item, request.transform))
                        {
                            const Vector3& location = request.transform.translation;
                            const Vector4 clip = projection * view * Vector4(location.x, location.y, location.z, 1);
                            if (clip.w > 0)
                            {
                                const ImVec2 marker(origin.x + (clip.x / clip.w + 1) * 0.5f * available.x,
                                                    origin.y + (1 - clip.y / clip.w) * 0.5f * available.y);
                                ImDrawList* draw = ImGui::GetWindowDrawList();
                                draw->PushClipRect(origin, ImVec2(origin.x + available.x, origin.y + available.y), true);
                                draw->AddCircle(marker, 12.0f, IM_COL32(255, 200, 70, 255), 16, 2.0f);
                                draw->AddText(ImVec2(marker.x + 15, marker.y), IM_COL32_WHITE, item->name);
                                draw->PopClipRect();
                            }
                            if (payload->IsDelivery())
                            {
                                const auto actor_id = history.place_actor(world, request);
                                if (actor_id != 0)
                                {
                                    selection.select_actor(world, actor_id);
                                    cancel_pending_hit();
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }


                bool gizmo_consumed_click = false;
                Actor* const selected = selection.resolve_actor(world);
                std::uint32_t hovered_light_id = 0;
                {
                    Quaternion orientation;
                    Vector3 direction;
                    Matrix4 view;
                    Matrix4 projection;
                    PerspectiveProjectionDesc desc;
                    desc.vertical_fov = to_radians(Degrees(60.0f));
                    desc.aspect = static_cast<float>(scene_extent_.width) / scene_extent_.height;
                    desc.near_clip = k_near_clip;
                    desc.far_clip = k_far_clip;
                    if (make_camera(orientation, direction) &&
                        try_make_view_matrix(k_camera_position, orientation, view) &&
                        try_make_perspective_projection(desc, projection))
                        hovered_light_id = draw_light_actor_icons(world, projection * view,
                            Vector2(origin.x, origin.y), Vector2(available.x, available.y),
                            selected ? selected->actor_id() : 0u, hovered && !placement_active);
                }
                SceneComponent* const root = selected != nullptr ? selected->root_component() : nullptr;
                if (root != nullptr && !placement_active)
                {
                    Quaternion orientation;
                    Vector3 direction;
                    Matrix4 view;
                    Matrix4 projection;
                    PerspectiveProjectionDesc desc;
                    desc.vertical_fov = to_radians(Degrees(60.0f));
                    desc.aspect = static_cast<float>(scene_extent_.width) /
                                  static_cast<float>(scene_extent_.height);
                    desc.near_clip = k_near_clip;
                    desc.far_clip = k_far_clip;
                    if (make_camera(orientation, direction) &&
                        try_make_view_matrix(k_camera_position, orientation, view) &&
                        try_make_perspective_projection(desc, projection))
                    {
                        if (history.active_for(EditorTransformSource::Details) &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            history.finish(world, EditorTransformSource::Details);
                        const Transform before = root->local_transform();
                        gizmo_consumed_click = gizmo_.manipulate(*root, view, projection, origin.x, origin.y,
                                                                 available.x, available.y);
                        if (ImGuizmo::IsUsingAny() && !history.active())
                            history.begin(world, selected->actor_id(), before, EditorTransformSource::Gizmo);
                    }
                }
                if (history.active_for(EditorTransformSource::Gizmo) && !ImGuizmo::IsUsingAny())
                    history.finish(world, EditorTransformSource::Gizmo);
                if (gizmo_consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    current_hit_request_id_ = 0u;
                if (hovered && !placement_active && !gizmo_consumed_click && !ImGui::GetIO().WantTextInput &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // Editor overlay icons use their visible screen rectangle.
                    // Cancel an older asynchronous GPU result before selecting;
                    // mesh clicks continue through the existing HitProxy Pass.
                    if (hovered_light_id != 0)
                    {
                        selection.select_actor(world, hovered_light_id);
                        cancel_pending_hit();
                    }
                    else
                    {
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const std::uint32_t x = physical_pixel(mouse.x - origin.x, scale.x);
                        const std::uint32_t y = physical_pixel(mouse.y - origin.y, scale.y);
                        if (x < scene_extent_.width && y < scene_extent_.height &&
                            next_hit_request_id_ != (std::numeric_limits<std::uint64_t>::max)())
                        {
                            pending_hit_request_.request_id = next_hit_request_id_++;
                            pending_hit_request_.viewport_generation = viewport_generation_;
                            pending_hit_request_.scene_generation = world.scene_generation();
                            pending_hit_request_.pixel_x = x;
                            pending_hit_request_.pixel_y = y;
                            current_hit_request_id_ = pending_hit_request_.request_id;
                        }
                    }
                }
            }
        }
        ImGui::End();
        if (history.active_for(EditorTransformSource::Gizmo) && !ImGuizmo::IsUsingAny())
            history.finish(world, EditorTransformSource::Gizmo);
        if (scene_extent_ != previous_scene_extent_)
        {
            ++viewport_generation_;
            previous_scene_extent_ = scene_extent_;
            current_hit_request_id_ = 0u;
            if (pending_hit_request_.request_id != 0u)
            {
                pending_hit_request_.viewport_generation = viewport_generation_;
                current_hit_request_id_ = pending_hit_request_.request_id;
            }
        }
    }

    bool SceneViewport::extent(Extent& extent) const
    {
        extent = scene_extent_;
        return true;
    }

    bool SceneViewport::take_hit_request(HitProxyRequest& request)
    {
        if (pending_hit_request_.request_id == 0u)
            return false;
        request = pending_hit_request_;
        pending_hit_request_ = {};
        return true;
    }

    void SceneViewport::cancel_pending_hit()
    {
        pending_hit_request_ = {};
        current_hit_request_id_ = 0u;
    }

    void SceneViewport::receive_hit_result(World& world, EditorSelection& selection,
                                            const HitProxyResult& result)
    {
        if (result.request.request_id != current_hit_request_id_ ||
            result.request.viewport_generation != viewport_generation_ ||
            result.request.scene_generation != world.scene_generation())
            return;
        current_hit_request_id_ = 0u;
        if (result.target.kind == HitProxyTargetKind::None)
        {
            selection.clear_actor();
            return;
        }
        Actor* const actor = world.find_actor_by_id(result.target.actor_id);
        if (actor == nullptr || (result.target.kind != HitProxyTargetKind::Actor &&
                                 actor->find_component_by_id(result.target.component_id) == nullptr))
        {
            selection.clear_actor();
            return;
        }
        selection.select_actor(world, actor->actor_id());
    }

    void SceneViewport::build_scene_views(std::vector<SceneView>& views, const Extent& extent) const
    {
        Vector3 direction;
        Quaternion orientation;
        if (!make_camera(orientation, direction))
        {
            TOY_LOG_ERROR("Editor scene camera could not be constructed.");
            return;
        }
        views.emplace_back(k_camera_position, orientation, direction,
                           IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                           to_radians(Degrees(60.0f)), k_near_clip, k_far_clip);
    }
} // namespace toy3d
