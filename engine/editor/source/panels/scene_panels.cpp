#include "panels/scene_panels.h"

#include "imgui.h"

#include "commands/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/camera_component.h"
#include "gamescene/component/light_component.h"
#include "gamescene/component/primitive_component.h"
#include "gamescene/component/static_mesh_component.h"
#include "gamescene/world/world.h"
#include "logging/logger.h"
#include "selection/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "viewport/scene_viewport.h"

#include <cstdint>
#include <algorithm>
#include <string>
#include <cstring>

namespace toy3d
{
    namespace
    {
        void draw_material_slots(World& world, Actor& actor, EditorSelection& selection, EditorCommandHistory& history,
            const EditorWorkspace& workspace, MaterialAssignments& materials, std::string& error)
        {
            auto* component = dynamic_cast<StaticMeshComponent*>(actor.root_component());
            if (!component || !component->static_mesh()) return;
            ImGui::Separator();
            ImGui::TextUnformatted("Materials");
            const auto& names = component->static_mesh()->material_slot_names();
            for (const auto& name : names)
            {
                ImGui::PushID(name.c_str());
                ImGui::TextUnformatted(name.c_str());
                const AssetRef current = materials.reference(world, actor.actor_id(), component->component_id(), name);
                const AssetLocation* location = current.asset_id.valid() ? workspace.catalog().index.find(current.asset_id) : nullptr;
                const char* label = location ? location->path.utf8().c_str() : current.asset_id.valid() ? "Missing Material" : "Mesh Default";
                auto assign = [&](const AssetId& id)
                {
                    AssetRef reference;
                    if (id.valid())
                    {
                        const auto* asset = workspace.catalog().index.find(id);
                        if (!asset || (asset->index.root_type != "toy3d.MaterialAssetData" &&
                            asset->index.root_type != "toy3d.MaterialInstanceAssetData"))
                        { error = "The selected Material asset is missing or has changed type. Refresh Content Browser."; }
                        else { reference.asset_id = id; reference.expected_type = asset->index.root_type; }
                        if (!reference.asset_id.valid()) { TOY_LOG_ERROR("Material assignment: {}", error); return; }
                    }
                    if (!history.assign_material(world, actor.actor_id(), component->component_id(), name, reference, error))
                        TOY_LOG_ERROR("Material assignment: {}", error);
                    else selection.select_actor(world, actor.actor_id());
                };
                ImGui::BeginDisabled(history.active());
                ImGui::SetNextItemWidth(-80.0f);
                if (ImGui::BeginCombo("##Material", label))
                {
                    if (ImGui::Selectable("Mesh Default", !current.asset_id.valid())) assign({});
                    for (const auto& asset : workspace.catalog().entries)
                        if (asset.file.root_type == "toy3d.MaterialAssetData" || asset.file.root_type == "toy3d.MaterialInstanceAssetData")
                        {
                            ImGui::PushID(asset.file.asset_id.hex().c_str());
                            if (ImGui::Selectable(asset.path.utf8().c_str(), current.asset_id == asset.file.asset_id)) assign(asset.file.asset_id);
                            ImGui::PopID();
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(MATERIAL_ASSET_DRAG_PAYLOAD);
                    if (payload && payload->IsDelivery())
                    {
                        AssetId id;
                        if (payload->DataSize == sizeof(AssetId)) std::memcpy(&id, payload->Data, sizeof(id));
                        if (id.valid()) assign(id);
                        else { error = "The dragged material has an invalid asset identity."; TOY_LOG_ERROR("Material assignment: {}", error); }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!current.asset_id.valid());
                if (ImGui::Button("Reset")) assign({});
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::TextDisabled("%s", current.asset_id.valid() ? "Actor override" : "Mesh default material");
                ImGui::PopID();
            }
            if (!error.empty())
            {
                ImGui::TextWrapped("Material assignment failed: %s", error.c_str());
                if (ImGui::Button("Dismiss Material Error")) error.clear();
            }
        }

        void draw_transform_field(const char* label, Vector3 Transform::* field, World& world,
                                  Actor& actor, EditorCommandHistory& history)
        {
            SceneComponent* const root = actor.root_component();
            Transform edited = root->local_transform();
            Vector3& value = edited.*field;
            const float speed = field == &Transform::scale ? 0.01f : 5.0f;
            const bool changed = ImGui::DragFloat3(label, value.data(), speed);
            if (ImGui::IsItemActivated() && !history.active())
                history.begin(world, actor.actor_id(), root->local_transform(), EditorTransformSource::Details);
            if (changed && !root->set_local_transform(edited))
                TOY_LOG_ERROR("Details rejected the {} Transform for Actor {}.", label, actor.actor_id());
            if (ImGui::IsItemDeactivated())
                history.finish(world, EditorTransformSource::Details);
        }
        void draw_camera_field(const char* label, float EditorActorState::* field, float speed,
                               World& world, Actor& actor, EditorCommandHistory& history)
        {
            EditorActorState edited = capture_actor_state(actor);
            const bool changed = ImGui::DragFloat(label, &(edited.*field), speed);
            if (ImGui::IsItemActivated())
                history.begin(world, actor.actor_id(), edited.transform, EditorTransformSource::Details);
            if (changed && !apply_actor_state(actor, edited))
                TOY_LOG_ERROR("Details rejected {} for Camera Actor {}.", label, actor.actor_id());
            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
        }
    } // namespace

    bool draw_outliner(World& world, EditorSelection& selection, EditorCommandHistory& history,
                       const ActorFactory& factory, SceneViewport& viewport)
    {
        bool changed = false;
        if (ImGui::Begin("Outliner"))
        {
            selection.resolve_actor(world);
            for (const std::uint32_t actor_id : world.actor_ids())
            {
                const std::string label = std::string(factory.label(actor_id)) + " " + std::to_string(actor_id);
                ImGui::PushID(static_cast<int>(actor_id));
                if (ImGui::Selectable(label.c_str(), selection.actor_id() == actor_id))
                {
                    history.finish(world, EditorTransformSource::Details);
                    selection.select_actor(world, actor_id);
                    changed = true;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    viewport.focus_actor(world, actor_id);
                ImGui::PopID();
            }
        }
        ImGui::End();
        return changed;
    }

    void draw_details(World& world, EditorSelection& selection, EditorCommandHistory& history,
                      const EditorWorkspace& workspace, SceneViewport& viewport,
                      MaterialAssignments& materials, std::string& material_error)
    {
        if (ImGui::Begin("Details"))
        {
            const auto* payload = ImGui::GetDragDropPayload();
            const bool material_drag = payload && payload->IsDataType(MATERIAL_ASSET_DRAG_PAYLOAD);
            // Asset browsing retains the last Actor identity. During a material
            // drag expose its slots even if the source tile had asset focus.
            if (selection.focus() == EditorSelectionFocus::Asset && !material_drag)
            {
                const AssetLocation* const asset = selection.resolve_asset(workspace.catalog().index);
                if (asset != nullptr)
                {
                    ImGui::TextUnformatted(asset->path.utf8().c_str());
                    ImGui::Separator();
                    ImGui::Text("Asset ID: %s", asset->index.asset_id.hex().c_str());
                    ImGui::Text("Type: %s", asset->index.root_type.c_str());
                    ImGui::Text("Schema version: %u", asset->index.schema_version);
                    ImGui::Text("Dependencies: %u", static_cast<unsigned>(asset->index.dependencies.size()));
                    ImGui::TextDisabled("Typed asset editing is not connected yet.");
                }
                else
                    ImGui::TextUnformatted("No Asset selected");
                ImGui::End();
                return;
            }
            Actor* const actor = selection.resolve_actor(world);
            if (actor == nullptr)
                ImGui::TextUnformatted("No Actor selected");
            else
            {
                ImGui::Text("Actor ID: %u", actor->actor_id());
                if (actor->root_component() != nullptr)
                {
                    draw_transform_field("Location (cm)", &Transform::translation, world, *actor, history);
                    draw_transform_field("Scale", &Transform::scale, world, *actor, history);
                    ImGui::TextDisabled("Rotation: use the viewport gizmo");
                    draw_material_slots(world, *actor, selection, history, workspace, materials, material_error);
                    if (dynamic_cast<PrimitiveComponent*>(actor->root_component()))
                    {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Shadows");
                        EditorActorState edited = capture_actor_state(*actor);
                        bool changed = ImGui::Checkbox("Cast Shadows", &edited.primitive_cast_shadows);
                        if (ImGui::IsItemActivated())
                            history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Primitive shadow cast edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        edited = capture_actor_state(*actor);
                        changed = ImGui::Checkbox("Receive Shadows", &edited.primitive_receives_shadows);
                        if (ImGui::IsItemActivated())
                            history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Primitive shadow receive edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                    }
                    if (dynamic_cast<CameraComponent*>(actor->root_component()))
                    {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Perspective Camera");
                        draw_camera_field("Vertical FOV (degrees)", &EditorActorState::camera_vertical_fov,
                                          0.25f, world, *actor, history);
                        draw_camera_field("Near Clip (cm)", &EditorActorState::camera_near_clip,
                                          1.0f, world, *actor, history);
                        draw_camera_field("Far Clip (cm)", &EditorActorState::camera_far_clip,
                                          100.0f, world, *actor, history);
                        ImGui::TextDisabled("Aspect ratio follows the viewport");
                        ImGui::TextDisabled("Camera scale does not affect projection");
                        if (viewport.viewed_camera_id(world) == actor->actor_id())
                        {
                            if (ImGui::Button("Exit Camera View")) viewport.exit_camera_view();
                        }
                        else if (ImGui::Button("View Camera"))
                        {
                            history.finish(world, EditorTransformSource::Details);
                            if (!history.active()) viewport.view_camera(world, actor->actor_id());
                        }
                    }
                    auto* light = dynamic_cast<LightComponent*>(actor->root_component());
                    if (light)
                    {
                        ImGui::Separator();
                        EditorActorState edited = capture_actor_state(*actor);
                        bool changed = ImGui::Checkbox("Enabled", &edited.light_enabled);
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light enabled edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        edited = capture_actor_state(*actor);
                        changed = ImGui::ColorEdit3("Light Color (linear)", edited.light_color.data());
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light color edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        edited = capture_actor_state(*actor);
                        changed = ImGui::DragFloat("Intensity", &edited.light_intensity, 0.05f, 0.0f, 10000.0f);
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light intensity edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        edited = capture_actor_state(*actor);
                        changed = ImGui::DragInt("Priority", &edited.light_priority, 1.0f);
                        if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                        if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light priority edit failed.");
                        if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        if (dynamic_cast<LocalLightComponent*>(light))
                        {
                            edited = capture_actor_state(*actor);
                            changed = ImGui::DragFloat("Range (cm)", &edited.light_range, 10.0f, 1.0f, 1000000.0f);
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Light range edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                        }
                        if (dynamic_cast<DirectionalLightComponent*>(light) &&
                            ImGui::CollapsingHeader("Shadow Map", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            edited = capture_actor_state(*actor);
                            changed = ImGui::Checkbox("Cast Shadows", &edited.shadow_cast_shadows);
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Shadow toggle edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                            ImGui::BeginDisabled(!capture_actor_state(*actor).shadow_cast_shadows);
                            edited = capture_actor_state(*actor);
                            changed = ImGui::DragFloat("Dynamic Shadow Distance (cm)", &edited.shadow_distance, 50.0f, 0.0f, 1000000.0f);
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Shadow distance edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                            edited = capture_actor_state(*actor);
                            float fade_percent = edited.shadow_distance_fade_fraction * 100.0f;
                            changed = ImGui::DragFloat("Distance Fade (%)", &fade_percent, 0.1f, 0.0f, 99.9f);
                            edited.shadow_distance_fade_fraction = fade_percent * 0.01f;
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Shadow fade edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                            edited = capture_actor_state(*actor);
                            changed = ImGui::DragFloat("Shadow Bias", &edited.shadow_bias, 0.005f, 0.0f, 1.0f);
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Shadow bias edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                            edited = capture_actor_state(*actor);
                            changed = ImGui::DragFloat("Shadow Slope Bias", &edited.shadow_slope_bias, 0.005f, 0.0f, 1.0f);
                            if (ImGui::IsItemActivated()) history.begin(world, actor->actor_id(), edited.transform, EditorTransformSource::Details);
                            if (changed && !apply_actor_state(*actor, edited)) TOY_LOG_ERROR("Shadow slope bias edit failed.");
                            if (ImGui::IsItemDeactivated()) history.finish(world, EditorTransformSource::Details);
                            ImGui::EndDisabled();
                        }
                    }

                }
            }
        }
        ImGui::End();
    }

} // namespace toy3d
