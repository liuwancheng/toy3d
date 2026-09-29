#include "panels/material_create_dialog.h"

#include <cstring>

#include "imgui.h"
#include "logging/logger.h"
#include "selection/editor_selection.h"
#include "workspace/editor_workspace.h"

namespace toy3d
{
    void MaterialCreateDialog::clear()
    {
        active_ = false;
        open_ = false;
        name_.fill(0);
        folder_.clear();
        parent_ = {};
        published_id_ = {};
        error_.clear();
        two_sided_ = false;
    }

    void MaterialCreateDialog::request(MaterialAssetCreationKind kind, const std::string& folder, AssetId parent)
    {
        if (active_) return;
        clear();
        kind_ = kind;
        folder_ = folder == "/Project" || folder.compare(0, 9, "/Project/") == 0 ? folder : "";
        parent_ = parent;
        const char* name = kind == MaterialAssetCreationKind::Material ? "M_NewMaterial" : "MI_NewMaterial";
        std::memcpy(name_.data(), name, std::strlen(name) + 1u);
        active_ = true;
        open_ = true;
    }

    void MaterialCreateDialog::draw(EditorWorkspace& workspace, EditorSelection& selection,
        std::string& browser_folder, const shader::ShaderParameterSchema& schema)
    {
        if (!active_) return;
        const char* title = kind_ == MaterialAssetCreationKind::Material ? "Create Material" : "Create Material Instance";
        if (open_) { ImGui::OpenPopup(title); open_ = false; }
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::BeginDisabled(published_id_.valid());
        ImGui::SetNextItemWidth(360);
        ImGui::InputText("Asset Name", name_.data(), name_.size());
        ImGui::SetNextItemWidth(360);
        if (ImGui::BeginCombo("Directory", folder_.empty() ? "Choose a Project directory" : folder_.c_str()))
        {
            for (const auto& path : workspace.catalog().directories)
            {
                if (path.utf8() != "/Project" && path.utf8().compare(0, 9, "/Project/") != 0) continue;
                if (ImGui::Selectable(path.utf8().c_str(), folder_ == path.utf8())) folder_ = path.utf8();
            }
            ImGui::EndCombo();
        }
        if (kind_ == MaterialAssetCreationKind::Material)
        {
            ImGui::TextUnformatted("Shader: Toy3d/Surface/Phong");
            ImGui::TextDisabled("Unlit will be available after Sampler support is connected.");
            ImGui::Checkbox("Two Sided", &two_sided_);
        }
        else
        {
            const auto* parent = workspace.catalog().index.find(parent_);
            ImGui::SetNextItemWidth(360);
            if (ImGui::BeginCombo("Parent Material", parent ? parent->path.utf8().c_str() : "Choose a root Material"))
            {
                for (const auto& entry : workspace.catalog().entries)
                {
                    if (entry.file.root_type != "toy3d.MaterialAssetData") continue;
                    if (ImGui::Selectable(entry.path.utf8().c_str(), parent_ == entry.file.asset_id)) parent_ = entry.file.asset_id;
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Inherits Shader and Two Sided from its root Material.");
        }
        std::string destination;
        std::string destination_error;
        const bool valid_destination = material_asset_destination(folder_, name_.data(), destination, destination_error);
        if (valid_destination) ImGui::TextWrapped("Save to: %s", destination.c_str());
        else ImGui::TextDisabled("%s", destination_error.c_str());
        ImGui::EndDisabled();
        if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        ImGui::Separator();
        const bool can_create = valid_destination && !published_id_.valid() &&
            (kind_ == MaterialAssetCreationKind::Material || parent_.valid());
        ImGui::BeginDisabled(!can_create);
        if (ImGui::Button("Create"))
        {
            AssetStatus result;
            if (kind_ == MaterialAssetCreationKind::Material)
            {
                MaterialAssetData data;
                data.shader_name = "Toy3d/Surface/Phong";
                data.two_sided = two_sided_;
                result = create_material_asset_in_workspace(workspace, destination, data, schema, published_id_);
            }
            else
            {
                MaterialInstanceAssetData data;
                data.parent.asset_id = parent_;
                data.parent.expected_type = "toy3d.MaterialAssetData";
                result = create_material_instance_asset_in_workspace(workspace, destination, data, schema, published_id_);
            }
            if (result.succeeded())
            {
                browser_folder = folder_;
                selection.select_asset(published_id_);
                TOY_LOG_INFO("Created material asset: {} ({})", destination, published_id_.hex());
                active_ = false;
                ImGui::CloseCurrentPopup();
            }
            else { error_ = result.message; TOY_LOG_ERROR("Material creation failed: {}", error_); }
        }
        ImGui::EndDisabled();
        if (published_id_.valid() && active_)
        {
            ImGui::SameLine();
            if (ImGui::Button("Refresh Saved Asset"))
            {
                if (workspace.refresh())
                {
                    browser_folder = folder_;
                    selection.select_asset(published_id_);
                    active_ = false;
                    ImGui::CloseCurrentPopup();
                }
                else { error_ = workspace.error(); TOY_LOG_ERROR("Material catalog refresh failed: {}", error_); }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(published_id_.valid() ? "Close" : "Cancel"))
        { clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
