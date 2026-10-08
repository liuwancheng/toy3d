#include "panels/scene_panels.h"
#include "scene/actor_details.h"

#include <chrono>
#include <string>
#include "imgui.h"
#include "panels/property_widgets.h"
#include "imgui_internal.h"
#include "asset_loader/asset_loader.h"
#include "rendercore/texture/texture_load_job.h"
#include "math/angle.h"
#include <cmath>
#include "scene/editor_command_history.h"
#include "scene/placement/asset_placement.h"
#include "scene/components/component_details.h"
#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "scene/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "viewport/scene_viewport.h"

namespace toy3d
{
    void draw_world_settings(World& world, const EditorWorkspace& workspace, EditorCommandHistory& history,
                             AssetLoader* assets, std::string& error)
    {
        if (ImGui::Begin("World Settings"))
        {
            SceneEnvironmentSettings settings = world.environment_settings();
            const auto* current = workspace.catalog().index.find(settings.environment.asset_id);
            const char* label = current ? current->path.utf8().c_str()
                                        : (settings.environment.asset_id.valid() ? "Missing environment" : "Off");
            if (begin_property_row("Reflection Environment"))
            {
                if (ImGui::BeginCombo("##Environment", label))
                {
                    if (ImGui::Selectable("Off", !settings.environment.asset_id.valid()))
                    {
                        settings.environment = {};
                        if (!history.set_environment(world, settings, {}))
                        {
                            error = history.error();
                        }
                        else
                        {
                            error.clear();
                        }
                    }
                    for (const auto& asset : workspace.catalog().entries)
                    {
                        if (asset.file.root_type != "toy3d.EnvironmentAssetData")
                        {
                            continue;
                        }
                        if (ImGui::Selectable(asset.path.utf8().c_str(),
                                              asset.file.asset_id == settings.environment.asset_id))
                        {
                            settings.environment = {
                                asset.file.asset_id, {}, "toy3d.EnvironmentAssetData", AssetRefStrength::Strong};
                            if (assets == nullptr)
                            {
                                error = "World environment: the asset loader is unavailable.";
                            }
                            else
                            {
                                // A World environment change is committed in the same frame, so
                                // this one Critical decode is waited for instead of leaving the
                                // World on a half-applied setting.
                                std::string load_error;
                                const auto loaded = load_assembly_texture(*assets, settings.environment,
                                                                          workspace.catalog().index, load_error);
                                if (!loaded)
                                {
                                    error = load_error;
                                }
                                else if (!history.set_environment(world, settings, loaded))
                                {
                                    error = history.error();
                                }
                                else
                                {
                                    error.clear();
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                end_property_row();
            }
            settings = world.environment_settings();
            const auto finish_edit = [&]()
            {
                if (ImGui::IsItemDeactivated())
                {
                    history.finish(world, EditorTransformSource::WorldSettings);
                }
            };
            if (property_float("Intensity", &settings.intensity, 0.01f, 0.0f, 0.0f, "%.3f"))
            {
                if (!history.preview_environment(world, settings, world.environment_cube()))
                {
                    error = history.error();
                }
                else
                {
                    error.clear();
                }
            }
            finish_edit();
            // Relative world-axis rotations preserve the complete quaternion without Euler singularities.
            constexpr std::size_t rotation_axis_count = 3u;
            const char* rotation_labels[rotation_axis_count] = {
                "Rotate around X (degrees)", "Rotate around Y (degrees)", "Rotate around Z (degrees)"};
            const Vector3 rotation_axes[rotation_axis_count] = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
            for (std::size_t axis = 0u; axis < rotation_axis_count; ++axis)
            {
                float rotation_delta_degrees = 0.0f;
                if (property_float(rotation_labels[axis], &rotation_delta_degrees, 0.5f, 0.0f, 0.0f, "%.1f"))
                {
                    settings = world.environment_settings();
                    Quaternion delta;
                    if (try_make_quaternion_from_axis_angle(
                            rotation_axes[axis], Radians(rotation_delta_degrees * 0.017453292519943295f), delta))
                    {
                        settings.rotation = delta * settings.rotation;
                        if (!history.preview_environment(world, settings, world.environment_cube()))
                        {
                            error = history.error();
                        }
                        else
                        {
                            error.clear();
                        }
                    }
                }
                finish_edit();
            }
            if (ImGui::Button("Reset Rotation"))
            {
                settings = world.environment_settings();
                settings.rotation = Quaternion::identity();
                if (!history.set_environment(world, settings, world.environment_cube()))
                {
                    error = history.error();
                }
            }
            ImGui::TextDisabled("Specular reflection only; scene lights provide diffuse lighting.");
            if (!error.empty())
            {
                ImGui::TextWrapped("%s", error.c_str());
            }
        }
        ImGui::End();
        if (history.active_for(EditorTransformSource::WorldSettings))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                history.cancel();
                ImGui::ClearActiveID();
            }
            else if (!ImGui::IsAnyItemActive())
            {
                history.finish(world, EditorTransformSource::WorldSettings);
            }
        }
    }

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
                {
                    viewport.focus_actor(world, actor_id);
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
        return changed;
    }

    void draw_details(World& world, EditorSelection& selection, EditorCommandHistory& history,
                      const EditorWorkspace& workspace, SceneViewport& viewport, MaterialAssignments& materials,
                      std::string& error, AssetResourcePicker* picker, MeshAssetBindings* bindings)
    {
        if (ImGui::Begin("Details"))
        {
            const auto* payload = ImGui::GetDragDropPayload();
            const bool material_drag = payload && (payload->IsDataType(MATERIAL_ASSET_DRAG_PAYLOAD) ||
                                                   payload->IsDataType(ASSET_DRAG_PAYLOAD));
            // Browsing retains the last Actor so material drag delivery can expose its slots.
            if (selection.focus() == EditorSelectionFocus::Asset && !material_drag)
            {
                const AssetLocation* asset = selection.resolve_asset(workspace.catalog().index);
                if (asset)
                {
                    ImGui::TextUnformatted(asset->path.utf8().c_str());
                    ImGui::Text("Asset ID: %s", asset->index.asset_id.hex().c_str());
                    ImGui::Text("Type: %s", asset->index.root_type.c_str());
                    ImGui::Text("Schema version: %u", asset->index.schema_version);
                    ImGui::Text("Dependencies: %u", static_cast<unsigned>(asset->index.dependencies.size()));
                }
                else
                {
                    ImGui::TextUnformatted("No Asset selected");
                }
            }
            else if (Actor* actor = selection.resolve_actor(world))
            {
                ImGui::Text("Actor ID: %u", actor->actor_id());
                draw_actor_details(*actor, history, workspace.types(), error);
                for (const auto id : actor->component_ids())
                {
                    auto* component = dynamic_cast<SceneComponent*>(actor->find_component_by_id(id));
                    if (!component)
                    {
                        continue;
                    }
                    const auto* editor = history.component_editors().find(*component);
                    ImGui::PushID(static_cast<int>(id));
                    if (!editor)
                    {
                        ImGui::TextDisabled("Unsupported component: %u", id);
                    }
                    else if (ImGui::CollapsingHeader(editor->display_name, ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ComponentDetailsContext context{world,    *actor,    *component, history, workspace, selection,
                                                        viewport, materials, error,      picker,  bindings};
                        ImGui::TextDisabled("Component: %u%s", id,
                                            component == actor->root_component() ? " (Root)" : "");
                        draw_component_transform(context);
                        editor->draw_details(context);
                    }
                    ImGui::PopID();
                }
            }
            else
            {
                ImGui::TextUnformatted("No Actor selected");
            }
        }
        ImGui::End();
        // A collapsed/hidden control no longer submits its deactivation callback.
        // End its gesture once ImGui releases the active item, even with the viewport hidden.
        if (history.active_for(EditorTransformSource::Details) && !ImGui::IsAnyItemActive())
        {
            history.finish(world, EditorTransformSource::Details);
        }
    }
} // namespace toy3d
