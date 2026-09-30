#include "panels/material_create_dialog.h"

#include <cstring>

#include "imgui.h"
#include "logging/logger.h"
#include "selection/editor_selection.h"
#include "workspace/editor_workspace.h"
#include "material/material_shader_workflow.h"

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
        shader_name_ = "Toy3d/Surface/Phong";
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
        std::string& browser_folder, const shader::ShaderParameterSchema& schema, MaterialShaderWorkflow* shaders)
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
            if (shaders && ImGui::BeginCombo("Shader", shader_name_.c_str()))
            {
                for (const auto& source : shaders->sources())
                    if (ImGui::Selectable(source.name.c_str(), source.name == shader_name_)) shader_name_ = source.name;
                ImGui::EndCombo();
            }
            if (!shaders) ImGui::TextUnformatted("Shader: Toy3d/Surface/Phong");
            ImGui::Checkbox("Two Sided", &two_sided_);
        }
        else
        {
            const auto* parent = workspace.catalog().index.find(parent_);
            ImGui::SetNextItemWidth(360);
            if (ImGui::BeginCombo("Parent Material", parent ? parent->path.utf8().c_str() : "Choose a Material or Instance"))
            {
                for (const auto& entry : workspace.catalog().entries)
                {
                    if (!is_material_asset_type(entry.file.root_type)) continue;
                    if (ImGui::Selectable(entry.path.utf8().c_str(), parent_ == entry.file.asset_id)) parent_ = entry.file.asset_id;
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Inherits Shader and Two Sided from its root Material.");
            if (parent)
            {
                AssetRef reference;
                reference.asset_id = parent_;
                reference.expected_type = parent->index.root_type;
                const auto hierarchy = read_material_hierarchy(workspace.types(), workspace.files(), workspace.catalog().index, reference);
                if (hierarchy.succeeded()) shader_name_ = hierarchy.value().root.shader_name;
                else error_ = hierarchy.status().message;
            }
        }
        const auto program = shaders ? shaders->program(shader_name_) : nullptr;
        if (shaders)
        {
            if (ImGui::Button("Open Source")) shaders->open_source(shader_name_);
            ImGui::SameLine(); ImGui::BeginDisabled(shaders->busy());
            if (ImGui::Button(program ? "Recompile Shader" : "Compile Shader")) shaders->recompile(shader_name_);
            ImGui::EndDisabled(); ImGui::TextWrapped("%s", shaders->status().c_str());
            if (!shaders->error().empty()) ImGui::TextWrapped("%s", shaders->error().c_str());
        }
        const auto selected_schema = program ? material_parameter_schema_from_shader_schema(program->data().parameter_schema) : schema;
        std::string destination;
        std::string destination_error;
        const bool valid_destination = material_asset_destination(folder_, name_.data(), destination, destination_error);
        if (valid_destination) ImGui::TextWrapped("Save to: %s", destination.c_str());
        else ImGui::TextDisabled("%s", destination_error.c_str());
        ImGui::EndDisabled();
        if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        ImGui::Separator();
        const bool can_create = valid_destination && !published_id_.valid() &&
            (kind_ == MaterialAssetCreationKind::Material || parent_.valid()) && (!shaders || program);
        ImGui::BeginDisabled(!can_create);
        if (ImGui::Button("Create"))
        {
            AssetStatus result;
            if (kind_ == MaterialAssetCreationKind::Material)
            {
                MaterialAssetData data;
                data.shader_name = shader_name_;
                data.two_sided = two_sided_;
                result = create_material_asset_in_workspace(workspace, destination, data, selected_schema, published_id_, shader_name_);
            }
            else
            {
                MaterialInstanceAssetData data;
                data.parent.asset_id = parent_;
                const auto* location = workspace.catalog().index.find(parent_);
                data.parent.expected_type = location ? location->index.root_type : "";
                result = create_material_instance_asset_in_workspace(workspace, destination, data, selected_schema, published_id_, shader_name_);
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
