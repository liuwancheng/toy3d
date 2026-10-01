#include "scene/components/component_details.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "scene/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/scene_component.h"
#include "logging/logger.h"

namespace toy3d
{
    bool capture_component_edit(ComponentDetailsContext& context, SceneComponentData& data)
    {
        return context.history.component_editors().capture(context.component, data);
    }

    void finish_component_edit(ComponentDetailsContext& context, const SceneComponentData& candidate, bool changed)
    {
        // Asset delivery owns the pointer gesture; it must not start a property edit.
        if (ImGui::GetDragDropPayload()) return;
        if (ImGui::IsItemActivated())
            context.history.begin(context.world, context.actor.actor_id(), context.actor.root_component()->local_transform(),
                                  EditorTransformSource::Details);
        if (changed && !context.history.preview_component(context.world, context.actor.actor_id(),
                                                         context.component.component_id(), candidate))
        {
            context.error = "Component rejected invalid properties.";
            TOY_LOG_ERROR("Details rejected {} properties.", candidate.type);
        }
        if (context.history.active_for(EditorTransformSource::Details) && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            context.history.cancel();
            // End ImGui's drag as well; otherwise the next mouse delta starts previewing a canceled gesture.
            ImGui::ClearActiveID();
        }
        else if (ImGui::IsItemDeactivated()) context.history.finish(context.world, EditorTransformSource::Details);
    }

    void draw_component_transform(ComponentDetailsContext& context)
    {
        SceneComponentData data;
        if (!capture_component_edit(context, data)) return;
        ImGui::BeginDisabled(ImGui::GetDragDropPayload() != nullptr);
        bool changed = ImGui::DragFloat3("Location (cm)", data.transform.translation.data(), meters_to_centimeters(0.05f));
        finish_component_edit(context, data, changed);
        if (!capture_component_edit(context, data)) { ImGui::EndDisabled(); return; }
        changed = ImGui::DragFloat3("Scale", data.transform.scale.data(), 0.01f);
        finish_component_edit(context, data, changed);
        ImGui::TextDisabled("Rotation: use the viewport gizmo");
        ImGui::EndDisabled();
    }

    void draw_node_details(ComponentDetailsContext&) {}
}
