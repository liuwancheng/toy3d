#include "panels/material_editor_panel.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"

#include "logging/logger.h"
#include "rendercore/shader/shader_map.h"
#include "workspace/editor_workspace.h"
#include "material/material_shader_workflow.h"

namespace toy3d
{
    namespace
    {
        AssetStatus parameter_error(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        const MaterialParameterOverride* find_override(const std::vector<MaterialParameterOverride>& values, const std::string& name)
        {
            for (const auto& value : values) if (value.name == name) return &value;
            return nullptr;
        }

        MaterialParameterOverride constant_default(const MaterialDesc& desc,
            const shader::ShaderParameterConstantMemberSchema& member)
        {
            MaterialParameterOverride value;
            value.name = member.name;
            if (member.type == shader::ShaderValueType::Float32) value.value = desc.scalar_defaults.at(member.parameter_id);
            else if (member.type == shader::ShaderValueType::Float32x2)
            {
                const auto& vector = desc.vector2_defaults.at(member.parameter_id);
                value.value = Vector2(vector.x, vector.y);
            }
            else if (member.type == shader::ShaderValueType::Float32x3)
            {
                const auto& vector = desc.vector3_defaults.at(member.parameter_id);
                value.value = Vector3(vector.x, vector.y, vector.z);
            }
            else if (member.type == shader::ShaderValueType::Float32x4)
            {
                const auto& vector = desc.vector4_defaults.at(member.parameter_id);
                value.value = Vector4(vector.x, vector.y, vector.z, vector.w);
            }
            return value;
        }
    }

    void MaterialEditorPanel::initialize(EditorWorkspace& workspace, MaterialRef defaults, const PhysicalPath& shader_root)
    {
        workspace_ = &workspace;
        defaults_ = std::move(defaults);
        textures_.named_defaults.clear();
        if (defaults_)
            for (const auto& resource : defaults_->parameter_schema().resources)
            {
                const auto found = defaults_->desc().texture_defaults.find(resource.parameter_id);
                if (found != defaults_->desc().texture_defaults.end()) textures_.named_defaults[resource.default_value] = found->second;
            }
        if (defaults_ && defaults_->desc().shader_program &&
            !workspace.read_material_properties(shader_root, defaults_->desc().shader_name,
                defaults_->desc().shader_program->data().parameter_schema, properties_, metadata_warning_))
            TOY_LOG_WARN("Material UI properties ignored: {}", metadata_warning_);
    }

    void MaterialEditorPanel::report(const AssetStatus& status)
    {
        if (status.succeeded()) { error_.clear(); return; }
        error_ = status.message;
        TOY_LOG_ERROR("Material edit [{} {}]: {}", status.asset_id.hex(), status.virtual_path, status.message);
    }

    void MaterialEditorPanel::request_open(const AssetId& id)
    {
        if (!workspace_) return;
        if (workspace_->material_edit().active() && workspace_->material_edit().id() == id)
        { focus_requested_ = true; return; }
        requested_ = id; close_requested_ = false; exit_requested_ = false;
    }
    void MaterialEditorPanel::request_close() { close_requested_ = true; requested_ = {}; }
    bool MaterialEditorPanel::request_exit()
    {
        if (!workspace_ || (!workspace_->material_edit().dirty() && !workspace_->material_edit().gesturing())) return true;
        exit_requested_ = true; close_requested_ = true; requested_ = {};
        return false;
    }
    bool MaterialEditorPanel::take_exit() { const bool ready = exit_ready_; exit_ready_ = false; return ready; }

    bool MaterialEditorPanel::resolve_unsaved(MaterialCloseDecision decision)
    {
        if (!workspace_ || !modal_pending()) return false;
        if (decision == MaterialCloseDecision::Cancel)
        {
            requested_ = {}; close_requested_ = false; exit_requested_ = false;
            return true;
        }
        if (decision == MaterialCloseDecision::Save)
        {
            const auto saved = workspace_->material_edit().save();
            report(saved);
            if (!saved.succeeded())
            {
                // A successful atomic publication may be followed by a failed
                // catalog refresh. Only this clean checkpoint can still close.
                if (saved.code != AssetErrorCode::InvalidState || workspace_->material_edit().dirty() ||
                    workspace_->material_edit().gesturing()) return false;
            }
        }
        else close();
        complete_transition();
        return true;
    }

    MaterialParameterChanges MaterialEditorPanel::parameter_changes(const std::vector<MaterialParameterOverride>& effective) const
    {
        MaterialParameterChanges result;
        const auto& schema = defaults_->parameter_schema();
        for (const auto& buffer : schema.constant_buffers)
            for (const auto& member : buffer.members)
            {
                MaterialParameterChange change;
                change.name = member.name;
                const auto* override_value = find_override(effective, member.name);
                if (override_value)
                {
                    // C++17 get_if maps persisted values into the runtime's closed
                    // value set without coupling RenderCore to editor snapshots.
                    if (const auto* value = std::get_if<float>(&override_value->value)) change.value = *value;
                    else if (const auto* value = std::get_if<Vector2>(&override_value->value)) change.value = *value;
                    else if (const auto* value = std::get_if<Vector3>(&override_value->value)) change.value = *value;
                    else if (const auto* value = std::get_if<Vector4>(&override_value->value)) change.value = *value;
                }
                result.push_back(std::move(change));
            }
        // Texture editing is opened together with the Texture2D production chain
        // in M5. Loaded texture defaults remain held by the immutable Material.
        return result;
    }

    bool MaterialEditorPanel::open(const AssetId& id)
    {
        if (!defaults_ || !defaults_->desc().shader_program) { report(parameter_error("The registered Phong Shader is unavailable.")); return false; }
        ShaderMapProgramRef program = defaults_->desc().shader_program;
        if (shaders_)
        {
            const auto* location = workspace_->catalog().index.find(id);
            if (!location) { report(parameter_error("Material asset is missing.")); return false; }
            MaterialAssetData root;
            AssetStatus read;
            if (location->index.root_type == "toy3d.MaterialInstanceAssetData")
            {
                MaterialInstanceAssetData child;
                read = read_material_instance_asset(workspace_->types(), workspace_->files(), location->path, child, &workspace_->catalog().index);
                if (!read.succeeded()) { report(read); return false; }
                const auto* parent = workspace_->catalog().index.find(child.parent.asset_id);
                if (!parent) { report(parameter_error("Parent material is missing.")); return false; }
                read = read_material_asset(workspace_->types(), workspace_->files(), parent->path, root, &workspace_->catalog().index);
            }
            else read = read_material_asset(workspace_->types(), workspace_->files(), location->path, root, &workspace_->catalog().index);
            if (!read.succeeded()) { report(read); return false; }
            program = shaders_->program(root.shader_name);
            if (!program) { report(parameter_error("Shader has no published Program. Compile it from Create Material first.")); return false; }
        }
        const auto schema = material_parameter_schema_from_shader_schema(program->data().parameter_schema);
        MaterialEditSession candidate(*workspace_);
        auto status = candidate.open(id, schema, program->data().shader_name);
        if (!status.succeeded()) { report(status); return false; }
        // Build the complete effective root first; the runtime object is private
        // to this window and does not mutate ActorFactory's shared default.
        MaterialAssetData effective = candidate.root_data();
        effective.overrides = candidate.effective_overrides();
        MaterialInstanceRef next;
        {
            const auto built = create_material_from_asset(effective, program, textures_);
            if (!built.succeeded()) { report(built.status()); return false; }
            next = built.value();
        }
        status = workspace_->material_edit().open(id, schema, program->data().shader_name);
        if (!status.succeeded()) { MaterialInstance::release(next); report(status); return false; }
        if (runtime_) MaterialInstance::release(runtime_);
        runtime_ = std::move(next);
        defaults_ = runtime_->material(); ++session_revision_;
        if (shaders_)
        {
            const auto* source = shaders_->find(program->data().shader_name);
            if (source) properties_ = source->properties;
        }
        workspace_->material_edit().set_preview(
            [this](const std::vector<MaterialParameterOverride>& values)
            {
                return runtime_->validate_parameters(parameter_changes(values)) ? AssetStatus::success() :
                    parameter_error("The complete parameter batch could not be prepared.");
            },
            [this](const std::vector<MaterialParameterOverride>& values)
            {
                try
                {
                    if (!runtime_->apply_parameters(parameter_changes(values)))
                        report(parameter_error("Material parameters could not be published."));
                }
                catch (const std::exception& exception) { report(parameter_error(exception.what())); }
            });
        error_.clear(); focus_requested_ = true;
        return true;
    }

    void MaterialEditorPanel::close()
    {
        discard_shader(); ++session_revision_;
        workspace_->material_edit().clear();
        if (runtime_) MaterialInstance::release(runtime_);
        focused_ = false;
    }

    bool MaterialEditorPanel::prepare_shader(const ShaderMapProgramRef& program,
        const std::vector<shader::ShaderEditorProperty>& properties, std::string& error)
    {
        discard_shader();
        auto& session = workspace_->material_edit();
        if (!session.active() || session.root_data().shader_name != program->data().shader_name) return true;
        if (session.gesturing()) { error = "Finish the parameter gesture before applying compiled code."; return false; }
        MaterialAssetData effective = session.root_data();
        auto schema = material_parameter_schema_from_shader_schema(program->data().parameter_schema);
        effective.overrides = session.effective_overrides(schema);
        candidate_properties_ = properties;
        candidate_schema_ = std::move(schema);
        const auto built = create_material_from_asset(effective, program, textures_);
        if (!built.succeeded()) { error = built.status().message; return false; }
        shader_candidate_ = built.value(); return true;
    }

    void MaterialEditorPanel::publish_shader()
    {
        if (!shader_candidate_) return;
        auto& session = workspace_->material_edit();
        const auto status = session.update_schema(std::move(candidate_schema_));
        if (!status.succeeded()) { report(status); discard_shader(); return; }
        if (runtime_) MaterialInstance::release(runtime_);
        runtime_ = std::move(shader_candidate_); defaults_ = runtime_->material();
        properties_ = std::move(candidate_properties_); metadata_warning_.clear(); error_.clear();
    }
    void MaterialEditorPanel::discard_shader()
    {
        if (shader_candidate_) MaterialInstance::release(shader_candidate_);
        candidate_properties_.clear();
        candidate_schema_ = {};
    }
    void MaterialEditorPanel::complete_transition()
    {
        if (requested_.valid()) open(requested_);
        else if (close_requested_) close();
        if (exit_requested_) exit_ready_ = true;
        requested_ = {}; close_requested_ = false; exit_requested_ = false;
    }
    void MaterialEditorPanel::shutdown()
    {
        if (workspace_) close();
        defaults_.reset(); textures_.named_defaults.clear(); workspace_ = nullptr;
    }
    void MaterialEditorPanel::undo()
    {
        if (workspace_ && workspace_->material_edit().undo_count()) report(workspace_->material_edit().undo());
    }
    void MaterialEditorPanel::redo()
    {
        if (workspace_ && workspace_->material_edit().redo_count()) report(workspace_->material_edit().redo());
    }
    void MaterialEditorPanel::save()
    {
        if (workspace_ && workspace_->material_edit().active()) report(workspace_->material_edit().save());
    }

    void MaterialEditorPanel::draw_parameters()
    {
        auto& session = workspace_->material_edit();
        struct ParameterRow
        {
            const shader::ShaderParameterConstantMemberSchema* member = nullptr;
            const shader::ShaderEditorProperty* property = nullptr;
        };
        std::vector<ParameterRow> rows;
        for (const auto& buffer : session.schema().constant_buffers)
            for (const auto& member : buffer.members)
            {
                ParameterRow row{&member, nullptr};
                for (const auto& property : properties_) if (property.name == member.name) row.property = &property;
                rows.push_back(row);
            }
        std::stable_sort(rows.begin(), rows.end(), [](const ParameterRow& a, const ParameterRow& b)
        {
            if (a.property && b.property) return a.property->display_order < b.property->display_order;
            if (a.property != nullptr || b.property != nullptr) return a.property != nullptr;
            return a.member->name < b.member->name;
        });
        for (const auto& row : rows)
        {
            const auto& member = *row.member;
            const auto effective = session.effective_overrides();
            const auto* inherited = find_override(effective, member.name);
            auto value = inherited ? *inherited : constant_default(defaults_->desc(), member);
            bool overridden = find_override(session.overrides(), member.name) != nullptr;
            ImGui::PushID(member.name.c_str());
            ImGui::BeginDisabled(!session.writable() || session.gesturing());
            if (ImGui::Checkbox("##Override", &overridden))
                report(overridden ? session.set_parameter(value) : session.remove_parameter(member.name));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(row.property ? row.property->display_name.c_str() : member.name.c_str());
            if (!defaults_->desc().shader_program->find_parameter_binding(member.parameter_id))
            { ImGui::SameLine(); ImGui::TextDisabled("(unused in this variant)"); }
            ImGui::BeginDisabled(!session.writable());
            ImGui::SetNextItemWidth(-90.0f);
            bool changed = false;
            // C++17 get_if selects familiar ImGui controls for the fixed numeric
            // alternatives. Shader UI metadata supplies presentation only.
            if (auto* scalar = std::get_if<float>(&value.value))
            {
                if (row.property && row.property->control == shader::ShaderEditorPropertyControl::Range &&
                    row.property->range_min && row.property->range_max)
                    changed = ImGui::SliderFloat("##Value", scalar, *row.property->range_min, *row.property->range_max);
                else changed = ImGui::DragFloat("##Value", scalar, 0.01f);
                if (changed && row.property)
                {
                    if (row.property->range_min) *scalar = std::max(*scalar, *row.property->range_min);
                    if (row.property->range_max) *scalar = std::min(*scalar, *row.property->range_max);
                }
            }
            else if (auto* vector = std::get_if<Vector2>(&value.value)) changed = ImGui::DragFloat2("##Value", vector->data(), 0.01f);
            else if (auto* vector = std::get_if<Vector3>(&value.value)) changed = ImGui::DragFloat3("##Value", vector->data(), 0.01f);
            else if (auto* vector = std::get_if<Vector4>(&value.value))
            {
                if (row.property && row.property->control == shader::ShaderEditorPropertyControl::Color)
                    changed = ImGui::ColorEdit4("##Value", vector->data(), ImGuiColorEditFlags_Float);
                else changed = ImGui::DragFloat4("##Value", vector->data(), 0.01f);
            }
            if (ImGui::IsItemActivated() && !session.gesturing()) report(session.begin_gesture());
            if (changed) report(session.set_parameter(value));
            if (ImGui::IsItemDeactivated() && session.gesturing()) report(session.finish_gesture());
            ImGui::SameLine();
            ImGui::BeginDisabled(!overridden || session.gesturing());
            if (ImGui::Button(session.is_instance() ? "Inherit" : "Reset")) report(session.remove_parameter(member.name));
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (!overridden) ImGui::TextDisabled(session.is_instance() && inherited ? "Inherited from parent" : "Shader default");
            ImGui::PopID();
        }
        for (const auto& resource : session.schema().resources)
            ImGui::TextDisabled("%s: %s (texture selection unavailable)", resource.name.c_str(), resource.default_value.c_str());
        const auto snapshot = session.overrides();
        for (const auto& value : snapshot)
        {
            if (material_override_matches_schema(value, session.schema())) continue;
            ImGui::PushID(value.name.c_str());
            ImGui::TextWrapped("Orphan: %s (parameter missing or type changed; preserved on save)", value.name.c_str());
            ImGui::BeginDisabled(!session.writable() || session.gesturing());
            if (ImGui::Button("Remove orphan")) report(session.remove_parameter(value.name));
            ImGui::EndDisabled(); ImGui::PopID();
        }
    }

    void MaterialEditorPanel::draw()
    {
        if (!workspace_) return;
        auto& session = workspace_->material_edit();
        if (modal_pending())
        {
            if (session.dirty() || session.gesturing()) ImGui::OpenPopup("Unsaved Material");
            else complete_transition();
        }
        if (ImGui::BeginPopupModal("Unsaved Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Save changes to %s?", session.path().utf8().c_str());
            if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
            if (ImGui::Button("Save"))
            {
                if (resolve_unsaved(MaterialCloseDecision::Save)) ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                // Drop the entire session before switching; no extra undo stack
                // or write is performed for the discarded draft.
                if (resolve_unsaved(MaterialCloseDecision::Discard)) ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                if (resolve_unsaved(MaterialCloseDecision::Cancel)) ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!session.active())
        {
            focused_ = false;
            if (!error_.empty())
            {
                ImGui::Begin("Material Editor");
                ImGui::TextWrapped("%s", error_.c_str());
                if (ImGui::Button("Dismiss")) error_.clear();
                ImGui::End();
            }
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(640, 600), ImGuiCond_FirstUseEver);
        if (focus_requested_) { ImGui::SetNextWindowFocus(); focus_requested_ = false; }
        bool visible = true;
        const bool drawn = ImGui::Begin("Material Editor", &visible,
            session.dirty() || session.gesturing() ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None);
        focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!visible) request_close();
        if (drawn)
        {
            ImGui::TextWrapped("%s%s", session.path().utf8().c_str(), session.dirty() ? " *" : "");
            ImGui::TextDisabled("%s%s", session.root_data().shader_name.c_str(), session.writable() ? "" : " | Read only");
            if (session.is_instance()) ImGui::TextDisabled("Parent: %s", session.instance_data()->parent.asset_id.hex().c_str());
            if (shaders_)
            {
                if (ImGui::Button("Open Source")) shaders_->open_source(session.root_data().shader_name);
                ImGui::SameLine();
                ImGui::BeginDisabled(shaders_->busy() || session.gesturing() || modal_pending());
                if (ImGui::Button("Recompile")) shaders_->recompile(session.root_data().shader_name, session.id(), session_revision_);
                ImGui::EndDisabled();
                ImGui::TextWrapped("%s", shaders_->status().c_str());
                if (!shaders_->error().empty()) ImGui::TextWrapped("%s", shaders_->error().c_str());
                if (shaders_->has_error_location() && ImGui::Button("Open Error in VS Code")) shaders_->open_error();
                if (!shaders_->output().empty() && ImGui::CollapsingHeader("Compiler Output")) ImGui::TextUnformatted(shaders_->output().c_str());
            }
            ImGui::TextDisabled("Two sided: %s", session.root_data().two_sided ? "Yes" : "No");
            ImGui::BeginDisabled(!session.writable() || modal_pending());
            if (ImGui::Button("Save")) save();
            ImGui::SameLine();
            ImGui::BeginDisabled(session.undo_count() == 0 || session.gesturing());
            if (ImGui::Button("Undo")) undo();
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(session.redo_count() == 0 || session.gesturing());
            if (ImGui::Button("Redo")) redo();
            ImGui::EndDisabled(); ImGui::EndDisabled();
            if (!metadata_warning_.empty()) ImGui::TextWrapped("Properties unavailable; using schema controls: %s", metadata_warning_.c_str());
            if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
            ImGui::Separator();
            if (focused_ && ImGui::IsKeyPressed(ImGuiKey_Escape) && session.gesturing())
            {
                report(session.cancel_gesture());
                ImGui::ClearActiveID();
            }
            draw_parameters();
            const auto& io = ImGui::GetIO();
            if (focused_ && !modal_pending() && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_S)) save();
                if (ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) redo(); else undo(); }
                else if (ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
            }
        }
        else if (session.gesturing()) report(session.cancel_gesture());
        ImGui::End();
    }
}
