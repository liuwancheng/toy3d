#include "panels/scene_panels.h"
#include "scene/actor_details.h"

#include <string>
#include "imgui.h"
#include "scene/editor_command_history.h"
#include "scene/components/component_details.h"
#include "gamescene/actor/actor.h"
#include "gamescene/world/world.h"
#include "scene/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "viewport/scene_viewport.h"

namespace toy3d
{
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
                      MaterialAssignments& materials, std::string& error)
    {
        if (ImGui::Begin("Details"))
        {
            const auto* payload = ImGui::GetDragDropPayload();
            const bool material_drag = payload && payload->IsDataType(MATERIAL_ASSET_DRAG_PAYLOAD);
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
                else ImGui::TextUnformatted("No Asset selected");
            }
            else if (Actor* actor = selection.resolve_actor(world))
            {
                ImGui::Text("Actor ID: %u", actor->actor_id());
                draw_actor_details(*actor, history, workspace.types(), error);
                for (const auto id : actor->component_ids())
                {
                    auto* component = dynamic_cast<SceneComponent*>(actor->find_component_by_id(id));
                    if (!component) continue;
                    const auto* editor = history.component_editors().find(*component);
                    ImGui::PushID(static_cast<int>(id));
                    if (!editor) ImGui::TextDisabled("Unsupported component: %u", id);
                    else if (ImGui::CollapsingHeader(editor->display_name, ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ComponentDetailsContext context{world, *actor, *component, history, workspace,
                                                        selection, viewport, materials, error};
                        ImGui::TextDisabled("Component: %u%s", id, component == actor->root_component() ? " (Root)" : "");
                        draw_component_transform(context);
                        editor->draw_details(context);
                    }
                    ImGui::PopID();
                }
            }
            else ImGui::TextUnformatted("No Actor selected");
        }
        ImGui::End();
        // A collapsed/hidden control no longer submits its deactivation callback.
        // End its gesture once ImGui releases the active item, even with the viewport hidden.
        if (history.active_for(EditorTransformSource::Details) && !ImGui::IsAnyItemActive())
            history.finish(world, EditorTransformSource::Details);
    }
}
