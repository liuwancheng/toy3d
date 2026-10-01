#include "panels/place_actors_panel.h"

#include "imgui.h"
#include "scene/placement/placement_catalog.h"
#include "viewport/actor_icons.h"

#include <cstring>

namespace toy3d
{
    void PlaceActorsPanel::draw()
    {
        if (ImGui::Begin("Place Actors"))
        {
            filter_.Draw("Search", -1.0f);
            ImGui::TextDisabled("Drag into the Scene Viewport");
            const char* category = nullptr;
            for (const PlacementItem& item : placement_catalog())
            {
                if (!filter_.PassFilter(item.name)) continue;
                if (category == nullptr || std::strcmp(category, item.category) != 0)
                {
                    category = item.category;
                    ImGui::Separator();
                    ImGui::TextUnformatted(category);
                }
                const bool is_directional = item.id == PlacementItemId::DirectionalLight;
                const bool is_point = item.id == PlacementItemId::PointLight;
                const bool is_camera = item.id == PlacementItemId::Camera;
                if (is_directional || is_point || is_camera)
                {
                    const ImVec2 position = ImGui::GetCursorScreenPos();
                    constexpr float row_height = 26.0f;
                    ImGui::PushID(static_cast<int>(item.id));
                    ImGui::Selectable("##actor", false, 0, ImVec2(0.0f, row_height));
                    if (is_camera)
                        draw_camera_icon(*ImGui::GetWindowDrawList(),
                                         Vector2(position.x + row_height * 0.5f, position.y + row_height * 0.5f), 24.0f);
                    else draw_light_icon(*ImGui::GetWindowDrawList(),
                                    is_directional ? LightKind::Directional : LightKind::Point,
                                    Vector2(position.x + row_height * 0.5f, position.y + row_height * 0.5f),
                                    24.0f);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(position.x + 32.0f, position.y + (row_height - ImGui::GetFontSize()) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), item.name);
                    ImGui::PopID();
                }
                else ImGui::Selectable(item.name);
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(PLACEMENT_DRAG_PAYLOAD, &item.id, sizeof(item.id));
                    ImGui::Text("Place %s", item.name);
                    ImGui::EndDragDropSource();
                }
            }
        }
        ImGui::End();
    }
}
