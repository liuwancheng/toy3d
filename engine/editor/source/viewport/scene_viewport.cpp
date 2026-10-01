#include "viewport/scene_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "imgui.h"
#include "ImGuizmo.h"

#include "commands/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/primitive_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "placement/actor_placement.h"
#include "selection/editor_selection.h"
#include "ui/imgui_draw_data.h"
#include "viewport/actor_icons.h"

namespace toy3d
{
    namespace
    {
        bool make_view_matrices(const SceneView& scene_view, Matrix4& view, Matrix4& projection)
        {
            PerspectiveProjectionDesc desc;
            desc.vertical_fov = scene_view.vertical_fov();
            desc.aspect = static_cast<float>(scene_view.output_extent().width) / scene_view.output_extent().height;
            desc.near_clip = scene_view.near_clip();
            desc.far_clip = scene_view.far_clip();
            return try_make_view_matrix(scene_view.camera_position(), scene_view.camera_orientation(), view) &&
                   try_make_perspective_projection(desc, projection);
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
    }

    SceneViewport::SceneViewport()
    {
        const Vector3 forward = normalize_unchecked(editor_camera_target_ - editor_camera_position_);
        editor_camera_yaw_ = std::atan2(forward.x, forward.z);
        editor_camera_pitch_ = std::asin(forward.y);
        if (!try_make_rotation_from_forward_up(editor_camera_target_ - editor_camera_position_,
                                               Vector3(0, 1, 0), editor_camera_orientation_))
            TOY_LOG_ERROR("Editor observation camera could not be initialized.");
    }

    bool SceneViewport::view_camera(const World& world, std::uint32_t actor_id)
    {
        const Actor* actor = world.find_actor_by_id(actor_id);
        if (!actor || actor->is_pending_destroy() || !dynamic_cast<const CameraComponent*>(actor->root_component()))
        {
            TOY_LOG_ERROR("Cannot view Camera Actor {} in the current World.", actor_id);
            return false;
        }
        if (camera_world_ != &world || camera_actor_id_ != actor_id)
        {
            camera_world_ = &world;
            camera_actor_id_ = actor_id;
            ++viewport_generation_;
            cancel_pending_hit();
        }
        return true;
    }

    bool SceneViewport::focus_actor(const World& world, std::uint32_t actor_id)
    {
        const Actor* actor = world.find_actor_by_id(actor_id);
        if (!actor || actor->is_pending_destroy() || !actor->root_component()) return false;
        Vector3 center = transform_position(actor->root_component()->world_transform(), Vector3());
        float radius = 100.0f;
        if (const auto* primitive = dynamic_cast<const PrimitiveComponent*>(actor->root_component()))
        {
            const AxisAlignedBounds& bounds = primitive->world_bounds();
            const Vector3 minimum(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
            const Vector3 maximum(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
            if (is_finite(minimum) && is_finite(maximum) &&
                bounds.minimum.x <= bounds.maximum.x && bounds.minimum.y <= bounds.maximum.y &&
                bounds.minimum.z <= bounds.maximum.z)
            {
                center = (minimum + maximum) * 0.5f;
                radius = std::max(50.0f, length(maximum - center));
            }
        }
        const float aspect = scene_extent_.height != 0u
            ? static_cast<float>(scene_extent_.width) / scene_extent_.height : 16.0f / 9.0f;
        const float half_vertical_fov = tan(to_radians(Degrees(60.0f)) * 0.5f);
        const float half_horizontal_fov = half_vertical_fov * std::max(aspect, 0.1f);
        const float distance = std::max(200.0f, radius * 1.25f /
            std::min(half_vertical_fov, half_horizontal_fov));
        const Vector3 forward = rotate_vector(editor_camera_orientation_, Vector3(0, 0, 1));
        const Vector3 position = center - forward * distance;
        if (!is_finite(center) || !is_finite(distance) || !is_finite(position)) return false;
        exit_camera_view();
        editor_camera_target_ = center;
        editor_camera_position_ = position;
        ++viewport_generation_;
        cancel_pending_hit();
        return true;
    }

    void SceneViewport::exit_camera_view()
    {
        if (camera_actor_id_ == 0) return;
        camera_actor_id_ = 0;
        camera_world_ = nullptr;
        ++viewport_generation_;
        cancel_pending_hit();
    }

    std::uint32_t SceneViewport::viewed_camera_id(const World& world) const
    {
        if (camera_world_ != &world || camera_actor_id_ == 0) return 0;
        const Actor* actor = world.find_actor_by_id(camera_actor_id_);
        return actor && !actor->is_pending_destroy() && dynamic_cast<const CameraComponent*>(actor->root_component())
            ? camera_actor_id_ : 0;
    }

    SceneView SceneViewport::current_view(const World& world, const Extent& extent) const
    {
        const Actor* actor = world.find_actor_by_id(viewed_camera_id(world));
        const auto* camera = actor ? dynamic_cast<const CameraComponent*>(actor->root_component()) : nullptr;
        const Vector3 position = camera ? transform_position(camera->world_transform(), Vector3()) : editor_camera_position_;
        const Quaternion orientation = camera ? camera->world_rotation() : editor_camera_orientation_;
        // Keep an orbit target in the editor frustum when zooming beyond the default range.
        const float editor_far_clip = std::max(100000.0f,
            length(editor_camera_target_ - editor_camera_position_) * 4.0f);
        return SceneView(position, orientation, rotate_vector(orientation, Vector3(0, 0, 1)),
                         IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                         to_radians(Degrees(camera ? camera->vertical_fov_degrees() : 60.0f)),
                         camera ? camera->near_clip() : 10.0f, camera ? camera->far_clip() : editor_far_clip);
    }

    void SceneViewport::begin_frame()
    {
        gizmo_.begin_frame();
        pending_hit_request_ = {};
        scene_extent_ = {};
        asset_placement_pending_ = false;
    }

    void SceneViewport::draw(World& world, EditorSelection& selection, EditorCommandHistory& history)
    {
        if (camera_actor_id_ != 0 && viewed_camera_id(world) == 0) exit_camera_view();
        // Keep the persisted ImGui window identity while changing its visible title.
        const bool visible = ImGui::Begin("Scene Viewport###Game Viewport", nullptr,
                                          ImGuiWindowFlags_NoScrollWithMouse);
        if (visible)
        {
            if (viewed_camera_id(world) != 0)
            {
                ImGui::Text("Viewing Camera %u", viewed_camera_id(world));
                ImGui::SameLine();
                if (ImGui::Button("Exit Camera View")) exit_camera_view();
            }
            else gizmo_.draw_toolbar();
            ImGui::TextDisabled("Wheel: zoom  |  Right drag: orbit  |  Middle drag: pan");
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
                const bool viewing = viewed_camera_id(world) != 0;
                ImGuiIO& io = ImGui::GetIO();
                if (viewing || ImGui::GetDragDropPayload() != nullptr)
                {
                    orbit_drag_active_ = false;
                    pan_drag_active_ = false;
                }
                else
                {
                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) orbit_drag_active_ = true;
                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) pan_drag_active_ = true;
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) orbit_drag_active_ = false;
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) pan_drag_active_ = false;
                    const Vector3 forward = rotate_vector(editor_camera_orientation_, Vector3(0, 0, 1));
                    float distance = length(editor_camera_target_ - editor_camera_position_);
                    bool camera_changed = false;
                    if (hovered && io.MouseWheel != 0.0f && std::isfinite(io.MouseWheel))
                    {
                        constexpr float min_distance = 20.0f;
                        constexpr float max_distance = 1000000.0f;
                        const float next_distance = std::max(min_distance,
                            std::min(max_distance, distance * std::pow(0.85f, io.MouseWheel)));
                        const Vector3 next_position = editor_camera_target_ - forward * next_distance;
                        if (is_finite(next_position) && next_position != editor_camera_position_)
                        {
                            editor_camera_position_ = next_position;
                            distance = next_distance;
                            camera_changed = true;
                        }
                    }
                    if (orbit_drag_active_ && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) &&
                        (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
                    {
                        constexpr float radians_per_pixel = 0.005f;
                        constexpr float pitch_limit = 1.48f;
                        const float yaw = editor_camera_yaw_ + io.MouseDelta.x * radians_per_pixel;
                        const float pitch = std::max(-pitch_limit, std::min(pitch_limit,
                            editor_camera_pitch_ - io.MouseDelta.y * radians_per_pixel));
                        const Vector3 next_forward(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                                   std::cos(yaw) * std::cos(pitch));
                        Quaternion orientation;
                        const Vector3 next_position = editor_camera_target_ - next_forward * distance;
                        if (is_finite(next_position) &&
                            try_make_rotation_from_forward_up(next_forward, Vector3(0, 1, 0), orientation))
                        {
                            editor_camera_yaw_ = yaw;
                            editor_camera_pitch_ = pitch;
                            editor_camera_orientation_ = orientation;
                            editor_camera_position_ = next_position;
                            camera_changed = true;
                        }
                    }
                    if (pan_drag_active_ && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) &&
                        (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
                    {
                        const float centimeters_per_pixel = 2.0f * distance *
                            tan(to_radians(Degrees(60.0f)) * 0.5f) / available.y;
                        const Vector3 right = rotate_vector(editor_camera_orientation_, Vector3(1, 0, 0));
                        const Vector3 up = rotate_vector(editor_camera_orientation_, Vector3(0, 1, 0));
                        const Vector3 offset = (up * io.MouseDelta.y - right * io.MouseDelta.x) * centimeters_per_pixel;
                        if (is_finite(offset) && is_finite(editor_camera_position_ + offset) &&
                            is_finite(editor_camera_target_ + offset))
                        {
                            editor_camera_position_ += offset;
                            editor_camera_target_ += offset;
                            camera_changed = true;
                        }
                    }
                    if (camera_changed)
                    {
                        ++viewport_generation_;
                        cancel_pending_hit();
                    }
                }
                if (!viewing) gizmo_.handle_shortcuts(hovered);
                const SceneView scene_view = current_view(world, scene_extent_);
                Matrix4 view;
                Matrix4 projection;
                const bool matrices_valid = make_view_matrices(scene_view, view, projection);
                if (!matrices_valid) TOY_LOG_ERROR("Editor viewport rejected camera matrices.");
                const bool placement_active = ImGui::GetDragDropPayload() != nullptr;
                if (!viewing && matrices_valid && ImGui::BeginDragDropTarget())
                {
                    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        PLACEMENT_DRAG_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery);
                    if (payload && payload->DataSize == sizeof(PlacementItemId))
                    {
                        const PlacementItemId item_id = *static_cast<const PlacementItemId*>(payload->Data);
                        const PlacementItem* item = find_placement_item(item_id);
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const Vector2 position((mouse.x - origin.x) / available.x, (mouse.y - origin.y) / available.y);
                        PlacementRequest request;
                        request.item = item_id;
                        if (item && calculate_placement_transform(view, projection, scene_view.camera_position(),
                                                                  position, *item, request.transform))
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
                    const ImGuiPayload* asset_payload = ImGui::AcceptDragDropPayload(
                        ASSET_DRAG_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery);
                    if (asset_payload && asset_payload->DataSize == sizeof(AssetId))
                    {
                        AssetPlacementRequest request;
                        std::memcpy(&request.asset_id, asset_payload->Data, sizeof(request.asset_id));
                        const PlacementItem mesh_item{PlacementItemId::StaticMesh, "Static Mesh", "Assets", 0};
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const Vector2 position((mouse.x - origin.x) / available.x, (mouse.y - origin.y) / available.y);
                        if (request.asset_id.valid() && calculate_placement_transform(view, projection,
                            scene_view.camera_position(), position, mesh_item, request.transform, &request.on_ground))
                        {
                            ImDrawList* draw = ImGui::GetWindowDrawList();
                            draw->PushClipRect(origin, ImVec2(origin.x + available.x, origin.y + available.y), true);
                            draw->AddCircle(mouse, 12, IM_COL32(255, 200, 70, 255), 16, 2);
                            draw->AddText(ImVec2(mouse.x + 15, mouse.y), IM_COL32_WHITE, "Place Static Mesh");
                            draw->PopClipRect();
                            if (asset_payload->IsDelivery())
                            {
                                asset_placement_ = request;
                                asset_placement_pending_ = true;
                                cancel_pending_hit();
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                bool gizmo_consumed_click = false;
                Actor* const selected = selection.resolve_actor(world);
                std::uint32_t hovered_icon_id = 0;
                if (!viewing && matrices_valid)
                    hovered_icon_id = draw_actor_icons(world, projection * view,
                        Vector2(origin.x, origin.y), Vector2(available.x, available.y),
                        selected ? selected->actor_id() : 0u, hovered && !placement_active,
                        static_cast<float>(scene_extent_.width) / scene_extent_.height);
                SceneComponent* const root = selected != nullptr ? selected->root_component() : nullptr;
                if (root != nullptr && !placement_active && !viewing && matrices_valid)
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
                if (history.active_for(EditorTransformSource::Gizmo) && !ImGuizmo::IsUsingAny())
                    history.finish(world, EditorTransformSource::Gizmo);
                if (gizmo_consumed_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    cancel_pending_hit();
                if (hovered && !viewing && matrices_valid && !placement_active && !gizmo_consumed_click &&
                    !ImGui::GetIO().WantTextInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // Lights and cameras share one depth-sorted overlay hit list.
                    // Invalidate older GPU results before accepting an icon click.
                    if (hovered_icon_id != 0)
                    {
                        selection.select_actor(world, hovered_icon_id);
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

    bool SceneViewport::take_asset_placement(AssetPlacementRequest& request)
    {
        if (!asset_placement_pending_) return false;
        request = asset_placement_;
        asset_placement_pending_ = false;
        return true;
    }

    bool SceneViewport::extent(Extent& extent) const
    {
        extent = scene_extent_;
        return true;
    }

    bool SceneViewport::take_hit_request(HitProxyRequest& request)
    {
        if (pending_hit_request_.request_id == 0u) return false;
        request = pending_hit_request_;
        pending_hit_request_ = {};
        return true;
    }

    void SceneViewport::cancel_pending_hit()
    {
        pending_hit_request_ = {};
        current_hit_request_id_ = 0u;
    }

    void SceneViewport::receive_hit_result(World& world, EditorSelection& selection, const HitProxyResult& result)
    {
        if (result.request.request_id == 0 || viewed_camera_id(world) != 0 ||
            result.request.request_id != current_hit_request_id_ ||
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

    void SceneViewport::build_scene_views(const World& world, std::vector<SceneView>& views, const Extent& extent) const
    {
        if (extent.width == 0 || extent.height == 0) return;
        views.push_back(current_view(world, extent));
    }
}
