#include "scene/components/component_details.h"

#include "imgui.h"
#include "panels/property_widgets.h"
#include "scene/editor_command_history.h"
#include "gamescene/actor/actor.h"

namespace toy3d
{
    namespace
    {
        void draw_light(ComponentDetailsContext& context, SceneComponentData& data, LightSettings& light)
        {
            bool changed = property_bool("Enabled", &light.enabled);
            finish_component_edit(context, data, changed);
            changed = property_color("Light Color (linear)", light.color.data());
            finish_component_edit(context, data, changed);
            changed = property_float("Intensity", &light.intensity, 0.05f, 0.0f, 10000.0f);
            finish_component_edit(context, data, changed);
            changed = property_int("Priority", &light.priority, 1.0f);
            finish_component_edit(context, data, changed);
        }
    } // namespace

    void draw_directional_light_details(ComponentDetailsContext& context)
    {
        SceneComponentData data;
        if (!capture_component_edit(context, data))
        {
            return;
        }
        // C++17 get_if selects directional author properties for this registered editor.
        auto* settings = std::get_if<SceneDirectionalLightData>(&data.properties);
        if (!settings)
        {
            return;
        }
        draw_light(context, data, settings->light);
        if (!ImGui::CollapsingHeader("Shadow Map", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        auto& shadow = settings->shadow;
        bool changed = property_bool("Cast Shadows", &shadow.cast_shadows);
        finish_component_edit(context, data, changed);
        ImGui::BeginDisabled(!shadow.cast_shadows);
        changed = property_slider_int("Num Dynamic Shadow Cascades", &shadow.cascade_count, 1,
                                      DirectionalShadowSettings::k_max_cascades);
        finish_component_edit(context, data, changed);
        ImGui::BeginDisabled(shadow.cascade_count == 1);
        changed = property_float("Cascade Distribution Exponent", &shadow.distribution_exponent, 0.02f, 0.1f, 10.0f);
        finish_component_edit(context, data, changed);
        ImGui::EndDisabled();
        const char* resolutions[] = {"512", "1024", "2048"};
        constexpr int k_resolution_count = static_cast<int>(sizeof(resolutions) / sizeof(resolutions[0]));
        int choice = shadow.map_resolution == 512 ? 0 : shadow.map_resolution == 1024 ? 1 : 2;
        changed = property_combo("Max Shadow Map Resolution", &choice, resolutions, k_resolution_count);
        if (changed)
        {
            context.history.begin(context.world, context.actor.actor_id(),
                                  context.actor.root_component()->local_transform(), EditorTransformSource::Details);
            shadow.map_resolution = DirectionalShadowSettings::k_min_resolution << choice;
            if (!context.history.preview_component(context.world, context.actor.actor_id(),
                                                   context.component.component_id(), data))
            {
                context.error = "Component rejected the shadow map resolution.";
            }
            context.history.finish(context.world, EditorTransformSource::Details);
        }
        changed = property_float("Dynamic Shadow Distance (cm)", &shadow.distance, meters_to_centimeters(0.5f), 0.0f,
                                 meters_to_centimeters(10000.0f));
        finish_component_edit(context, data, changed);
        float fade_percent = shadow.fade_fraction * 100.0f;
        changed = property_slider_float("Shadow Distance Fade (%)", &fade_percent, 0.0f, 99.0f);
        if (changed)
        {
            shadow.fade_fraction = fade_percent * 0.01f;
        }
        finish_component_edit(context, data, changed);
        changed = property_slider_float("Shadow Bias", &shadow.bias, 0, 1);
        finish_component_edit(context, data, changed);
        changed = property_slider_float("Shadow Slope Bias", &shadow.slope_bias, 0, 1);
        finish_component_edit(context, data, changed);
        changed = property_slider_float("Shadow Receiver Bias", &shadow.receiver_bias, 0, 1);
        finish_component_edit(context, data, changed);
        ImGui::EndDisabled();
    }

    void draw_point_light_details(ComponentDetailsContext& context)
    {
        SceneComponentData data;
        if (!capture_component_edit(context, data))
        {
            return;
        }
        // C++17 get_if selects point-light properties without a central type switch.
        auto* settings = std::get_if<ScenePointLightData>(&data.properties);
        if (!settings)
        {
            return;
        }
        draw_light(context, data, settings->light);
        const bool changed = property_float("Range (cm)", &settings->attenuation.range, meters_to_centimeters(0.1f),
                                            1.0f, meters_to_centimeters(10000.0f));
        finish_component_edit(context, data, changed);
    }
} // namespace toy3d
