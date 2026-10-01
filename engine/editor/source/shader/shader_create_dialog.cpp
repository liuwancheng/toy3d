#include "shader/shader_create_dialog.h"

#include <cstring>

#include "imgui.h"
#include "panels/editor_notifications.h"
#include "shader/shader_workflow.h"

namespace toy3d
{
    void ShaderCreateDialog::request()
    {
        if (active_) return;
        name_.fill(0); path_.fill(0);
        std::strcpy(name_.data(), "Project/Surface/NewShader");
        std::strcpy(path_.data(), "new_shader.shader");
        error_.clear(); created_name_.clear(); phong_ = false;
        active_ = open_ = true;
    }

    void ShaderCreateDialog::draw(ShaderWorkflow& shaders, EditorNotifications& notifications)
    {
        if (!active_) return;
        if (open_) { ImGui::OpenPopup("Create Shader"); open_ = false; }
        if (!ImGui::BeginPopupModal("Create Shader", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::BeginDisabled(!created_name_.empty());
        ImGui::SetNextItemWidth(390);
        ImGui::InputText("Logical Name", name_.data(), name_.size());
        ImGui::SetNextItemWidth(390);
        ImGui::InputText("Relative Path", path_.data(), path_.size());
        if (ImGui::BeginCombo("Template", phong_ ? "Phong" : "Unlit"))
        {
            if (ImGui::Selectable("Unlit", !phong_)) phong_ = false;
            if (ImGui::Selectable("Phong", phong_)) phong_ = true;
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Project Material Shader; saved to project/shader.");
        ImGui::TextDisabled("Create saves source and registers it. Compilation is a separate step.");
        if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        ImGui::Separator();
        if (created_name_.empty())
        {
            ImGui::BeginDisabled(shaders.busy());
            if (ImGui::Button("Create"))
            {
                if (shaders.create_source(name_.data(), path_.data(), phong_ ? "Toy3d/Surface/Phong" : "Toy3d/Surface/Unlit"))
                {
                    created_name_ = name_.data(); error_.clear();
                    notifications.success("Shader source created", created_name_ + " - ready to edit or compile.");
                }
                else error_ = shaders.error();
            }
            ImGui::EndDisabled();
        }
        else
        {
            if (ImGui::Button("Open Source") && !shaders.open_source(created_name_)) error_ = shaders.error();
            ImGui::SameLine(); ImGui::BeginDisabled(shaders.busy());
            if (ImGui::Button("Compile"))
            {
                if (shaders.recompile(created_name_)) { active_ = false; ImGui::CloseCurrentPopup(); }
                else error_ = shaders.error();
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button(created_name_.empty() ? "Cancel" : "Close"))
        { active_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
