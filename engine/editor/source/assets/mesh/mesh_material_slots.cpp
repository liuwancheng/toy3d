#include "mesh_material_slots.h"

#include <imgui.h>

#include "workspace/editor_workspace.h"

namespace toy3d
{
    bool draw_mesh_material_slot(AssetResourcePicker& picker, const EditorWorkspace& workspace, std::size_t index,
                                 const std::string& slot, const AssetRef& current, AssetRef& selected,
                                 std::string& error)
    {
        ImGui::PushID(slot.c_str());
        const auto label = "Element " + std::to_string(index);
        AssetResourceSelection choice;
        const bool changed = picker.draw(label.c_str(), workspace, {current.asset_id, {}},
                                         {"toy3d.MaterialAssetData", "toy3d.MaterialInstanceAssetData"}, choice, error);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Slot: %s", slot.c_str());
        }
        ImGui::TextDisabled("%s", slot.c_str());
        ImGui::PopID();
        if (changed)
        {
            selected = {};
            if (choice.asset.valid())
            {
                const auto* location = workspace.catalog().index.find(choice.asset);
                if (!location)
                {
                    error = "Selected material is no longer available.";
                    return false;
                }
                selected = {choice.asset, {}, location->index.root_type, AssetRefStrength::Strong};
            }
        }
        return changed;
    }
} // namespace toy3d
