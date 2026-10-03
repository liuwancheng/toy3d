#include "assets/preview/preview_scene_widgets.h"

#include "imgui.h"
#include "panels/property_widgets.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    bool draw_preview_scene_settings(const EditorWorkspace& workspace, PreviewSceneSettings& settings)
    {
        const auto previous = settings;
        std::string environment_name = settings.environment.valid() ? "Missing Environment" : "Off";
        if (const auto* location = workspace.catalog().index.find(settings.environment))
        {
            const auto& path = location->path.utf8();
            environment_name = path.substr(path.find_last_of('/') + 1);
        }
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (begin_property_row("Environment Asset"))
            {
                if (ImGui::BeginCombo("##Environment", environment_name.c_str()))
                {
                    if (ImGui::Selectable("Off", !settings.environment.valid()))
                    {
                        settings.environment = {};
                    }
                    for (const auto& entry : workspace.catalog().entries)
                    {
                        if (entry.file.root_type == "toy3d.EnvironmentAssetData" &&
                            ImGui::Selectable(entry.path.utf8().c_str(), entry.file.asset_id == settings.environment))
                        {
                            settings.environment = entry.file.asset_id;
                        }
                    }
                    ImGui::EndCombo();
                }
                end_property_row();
            }
            property_bool("Show Background", &settings.show_environment);
            property_slider_float("Environment Intensity", &settings.environment_intensity, 0.0f, 8.0f);
            property_slider_float("Environment Rotation", &settings.environment_rotation, -180.0f, 180.0f, "%.0f deg");
            property_slider_float("Exposure", &settings.exposure_ev, -8.0f, 8.0f, "%.2f EV");
        }
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            property_slider_float("Light Intensity", &settings.light_intensity, 0.0f, 16.0f);
            property_color("Light Color", settings.light_color.data());
            property_slider_float("Light Yaw", &settings.light_yaw, -180.0f, 180.0f, "%.0f deg");
            property_slider_float("Light Pitch", &settings.light_pitch, -89.0f, -5.0f, "%.0f deg");
            property_bool("Shadows", &settings.show_shadows);
        }
        if (ImGui::CollapsingHeader("Floor", ImGuiTreeNodeFlags_DefaultOpen))
        {
            property_bool("Show Floor", &settings.show_floor);
        }
        if (ImGui::Button("Reset Scene"))
        {
            settings = PreviewSceneSettings{};
        }
        return !(settings == previous);
    }
} // namespace toy3d
