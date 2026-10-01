#include "scene/components/component_details.h"

#include "imgui.h"
#include "scene/editor_command_history.h"
#include "gamescene/actor/actor.h"
#include "viewport/scene_viewport.h"

namespace toy3d
{
    void draw_camera_details(ComponentDetailsContext& context)
    {
        SceneComponentData data;
        if (!capture_component_edit(context, data)) return;
        // C++17 get_if retains the camera schema's concrete type in its Details implementation.
        auto* settings = std::get_if<CameraSettings>(&data.properties);
        if (!settings) return;
        bool changed = ImGui::DragFloat("Vertical FOV (degrees)", &settings->vertical_fov, 0.25f);
        finish_component_edit(context, data, changed);
        changed = ImGui::DragFloat("Near Clip (cm)", &settings->near_clip, 1.0f);
        finish_component_edit(context, data, changed);
        changed = ImGui::DragFloat("Far Clip (cm)", &settings->far_clip, meters_to_centimeters(1.0f));
        finish_component_edit(context, data, changed);
        ImGui::TextDisabled("Aspect ratio follows the viewport");
        // Current viewport viewing targets an Actor's root camera.
        if (&context.component != context.actor.root_component()) return;
        if (context.viewport.viewed_camera_id(context.world) == context.actor.actor_id())
        {
            if (ImGui::Button("Exit Camera View")) context.viewport.exit_camera_view();
        }
        else if (ImGui::Button("View Camera"))
        {
            context.history.finish(context.world, EditorTransformSource::Details);
            if (!context.history.active()) context.viewport.view_camera(context.world, context.actor.actor_id());
        }
    }
}
