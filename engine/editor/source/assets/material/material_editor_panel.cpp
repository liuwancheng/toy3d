#include "assets/material/material_editor_panel.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"

#include "logging/logger.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/texture/texture_asset_loader.h"
#include "workspace/editor_workspace.h"
#include "shader/shader_workflow.h"

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
        session_ = std::make_unique<MaterialEditSession>(workspace);
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
        if (edit_session().active() && edit_session().id() == id)
        { focus_requested_ = true; return; }
        requested_ = id; close_requested_ = false; exit_requested_ = false;
    }

    AssetStatus MaterialEditorPanel::ensure_texture_values(const std::vector<MaterialParameterOverride>& values)
    {
        for (const auto& item : values)
        {
            // C++17 get_if keeps asset-backed texture loading at the GT edge.
            const auto* reference = std::get_if<AssetRef>(&item.value);
            if (!reference || textures_.assets.count(reference->asset_id)) continue;
            const auto loaded = load_texture_asset(workspace_->files(), workspace_->catalog().index, *reference);
            if (!loaded.succeeded()) return loaded.status();
            textures_.assets.emplace(reference->asset_id, loaded.value());
        }
        return AssetStatus::success();
    }
    void MaterialEditorPanel::request_close() { close_requested_ = true; requested_ = {}; }
    bool MaterialEditorPanel::request_exit()
    {
        if (!workspace_ || (!edit_session().dirty() && !edit_session().gesturing())) return true;
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
            pending_save_failed_ = false;
            return true;
        }
        if (decision == MaterialCloseDecision::Save)
        {
            const auto saved = edit_session().save();
            report(saved);
            if (!saved.succeeded())
            {
                // Keep the reported failure visible even when the file reached
                // its clean checkpoint but catalog/render publication failed.
                pending_save_failed_ = true;
                return false;
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
        for (const auto& resource : schema.resources)
        {
            MaterialParameterChange change;
            change.name = resource.name;
            const auto* override_value = find_override(effective, resource.name);
            if (override_value)
            {
                if (const auto* reference = std::get_if<AssetRef>(&override_value->value))
                {
                    const auto found = textures_.assets.find(reference->asset_id);
                    if (found != textures_.assets.end()) change.value = found->second;
                }
                else if (const auto* sampler = std::get_if<MaterialSamplerPreset>(&override_value->value))
                    change.value = *sampler;
            }
            result.push_back(std::move(change));
        }
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
            AssetRef reference;
            reference.asset_id = id;
            reference.expected_type = location->index.root_type;
            const auto hierarchy = read_material_hierarchy(workspace_->types(), workspace_->files(), workspace_->catalog().index, reference);
            if (!hierarchy.succeeded()) { report(hierarchy.status()); return false; }
            const auto& root = hierarchy.value().root;
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
        status = ensure_texture_values(effective.overrides);
        if (!status.succeeded()) { report(status); return false; }
        MaterialInstanceRef next;
        {
            const auto built = create_material_from_asset(effective, program, textures_);
            if (!built.succeeded()) { report(built.status()); return false; }
            next = built.value();
        }
        status = edit_session().open(id, schema, program->data().shader_name);
        if (!status.succeeded()) { MaterialInstance::release(next); report(status); return false; }
        if (runtime_) MaterialInstance::release(runtime_);
        runtime_ = std::move(next);
        defaults_ = runtime_->material(); ++session_revision_;
        if (shaders_)
        {
            const auto* source = shaders_->find(program->data().shader_name);
            if (source) properties_ = source->properties;
        }
        edit_session().set_preview(
            [this](const std::vector<MaterialParameterOverride>& values)
            {
                const auto textures = ensure_texture_values(values);
                if (!textures.succeeded()) return textures;
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
        edit_session().set_parent_preview(
            [this](const std::string& name) -> AssetResult<shader::ShaderParameterSchema>
            {
                const auto program = shaders_ ? shaders_->program(name) :
                    (defaults_->desc().shader_name == name ? defaults_->desc().shader_program : nullptr);
                if (!program) return AssetResult<shader::ShaderParameterSchema>(parameter_error("Compile the Parent Shader before selecting it."));
                return AssetResult<shader::ShaderParameterSchema>(material_parameter_schema_from_shader_schema(program->data().parameter_schema));
            },
            [this](const MaterialAssetData& effective)
            {
                discard_shader();
                const auto textures = ensure_texture_values(effective.overrides);
                if (!textures.succeeded()) return textures;
                const auto program = shaders_ ? shaders_->program(effective.shader_name) : defaults_->desc().shader_program;
                const auto built = create_material_from_asset(effective, program, textures_);
                if (!built.succeeded()) return built.status();
                shader_candidate_ = built.value();
                candidate_schema_ = shader_candidate_->parameter_schema();
                if (shaders_)
                {
                    const auto* source = shaders_->find(effective.shader_name);
                    if (source) candidate_properties_ = source->properties;
                }
                return AssetStatus::success();
            },
            [this]() { publish_shader(); ++session_revision_; });
        error_.clear(); focus_requested_ = true;
        return true;
    }

    void MaterialEditorPanel::close()
    {
        discard_shader(); ++session_revision_;
        edit_session().clear();
        if (runtime_) MaterialInstance::release(runtime_);
        focused_ = false;
    }

    bool MaterialEditorPanel::prepare_shader(const ShaderMapProgramRef& program,
        const std::vector<shader::ShaderEditorProperty>& properties, std::string& error)
    {
        discard_shader();
        auto& session = edit_session();
        if (!session.active() || session.root_data().shader_name != program->data().shader_name) return true;
        if (session.gesturing()) { error = "Finish the parameter gesture before applying compiled code."; return false; }
        MaterialAssetData effective = session.root_data();
        auto schema = material_parameter_schema_from_shader_schema(program->data().parameter_schema);
        effective.overrides = session.effective_overrides(schema);
        const auto textures = ensure_texture_values(effective.overrides);
        if (!textures.succeeded()) { error = textures.message; return false; }
        candidate_properties_ = properties;
        candidate_schema_ = std::move(schema);
        const auto built = create_material_from_asset(effective, program, textures_);
        if (!built.succeeded()) { error = built.status().message; return false; }
        shader_candidate_ = built.value(); return true;
    }

    void MaterialEditorPanel::publish_shader()
    {
        if (!shader_candidate_) return;
        auto& session = edit_session();
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
        pending_save_failed_ = false;
    }
    void MaterialEditorPanel::shutdown()
    {
        if (session_) session_->set_publish({});
        if (workspace_) close();
        defaults_.reset(); textures_.named_defaults.clear(); textures_.assets.clear(); workspace_ = nullptr;
        session_.reset();
    }
    void MaterialEditorPanel::undo()
    {
        if (workspace_ && edit_session().undo_count()) report(edit_session().undo());
    }
    void MaterialEditorPanel::redo()
    {
        if (workspace_ && edit_session().redo_count()) report(edit_session().redo());
    }
    void MaterialEditorPanel::save()
    {
        if (workspace_ && edit_session().active()) report(edit_session().save());
    }

    void MaterialEditorPanel::draw_parameters()
    {
        auto& session = edit_session();
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
            if (!overridden)
            {
                const auto source = session.parameter_source(member.name);
                const auto* location = workspace_->catalog().index.find(source.asset_id);
                ImGui::TextDisabled("Inherited: %s", location ? location->path.utf8().c_str() : "Shader default");
            }
            ImGui::PopID();
        }
        for (const auto& resource : session.schema().resources)
        {
            ImGui::PushID(resource.name.c_str());
            const auto effective = session.effective_overrides();
            const auto* selected = find_override(effective, resource.name);
            const bool overridden = find_override(session.overrides(), resource.name) != nullptr;
            const char* title = resource.name.c_str();
            for (const auto& property : properties_)
                if (property.name == resource.name) { title = property.display_name.c_str(); break; }
            ImGui::TextUnformatted(title);
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.writable() || session.gesturing());
            if (resource.category == shader::ShaderParameterCategory::SampledTexture &&
                resource.resource_kind == shader::ResourceKind::Texture2D)
            {
                const auto* reference = selected ? std::get_if<AssetRef>(&selected->value) : nullptr;
                const auto* location = reference ? workspace_->catalog().index.find(reference->asset_id) : nullptr;
                const std::string label = location ? location->path.utf8() : "Shader default: " + resource.default_value;
                if (ImGui::BeginCombo("##Texture", label.c_str()))
                {
                    for (const auto& entry : workspace_->catalog().entries)
                    {
                        if (entry.file.root_type != "toy3d.Texture2DAssetData") continue;
                        if (ImGui::Selectable(entry.path.utf8().c_str(), reference && reference->asset_id == entry.file.asset_id))
                        {
                            AssetRef next;
                            next.asset_id = entry.file.asset_id;
                            next.expected_type = entry.file.root_type;
                            MaterialParameterOverride value{resource.name, next};
                            const auto ready = ensure_texture_values({value});
                            report(ready.succeeded() ? session.set_parameter(value) : ready);
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TOY3D_TEXTURE_ASSET"))
                    {
                        if (payload->DataSize == sizeof(AssetId))
                        {
                            const auto id = *static_cast<const AssetId*>(payload->Data);
                            const auto* entry = workspace_->catalog().index.find(id);
                            if (entry && entry->index.root_type == "toy3d.Texture2DAssetData")
                            {
                                AssetRef next;
                                next.asset_id = id;
                                next.expected_type = entry->index.root_type;
                                MaterialParameterOverride value{resource.name, next};
                                const auto ready = ensure_texture_values({value});
                                report(ready.succeeded() ? session.set_parameter(value) : ready);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else if (resource.category == shader::ShaderParameterCategory::Sampler &&
                resource.resource_kind == shader::ResourceKind::Sampler)
            {
                MaterialSamplerPreset preset{};
                if (selected)
                {
                    const auto* chosen = std::get_if<MaterialSamplerPreset>(&selected->value);
                    if (chosen) preset = *chosen;
                }
                else parse_material_sampler_preset(resource.default_value, preset);
                const auto ordinal = static_cast<std::size_t>(preset);
                const char* label = ordinal < shader::sampler_preset_count - 1u ?
                    shader::sampler_preset_name(static_cast<std::uint32_t>(ordinal)) : "Unsupported";
                if (ImGui::BeginCombo("##Sampler", label))
                {
                    for (std::uint32_t i = 0; i < shader::sampler_preset_count - 1u; ++i)
                        if (ImGui::Selectable(shader::sampler_preset_name(i), ordinal == i))
                            report(session.set_parameter({resource.name, static_cast<MaterialSamplerPreset>(i)}));
                    ImGui::EndCombo();
                }
            }
            else ImGui::TextDisabled("Unsupported resource type");
            ImGui::SameLine();
            ImGui::BeginDisabled(!overridden);
            if (ImGui::Button(session.is_instance() ? "Inherit" : "Reset")) report(session.remove_parameter(resource.name));
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (!overridden)
            {
                const auto source = session.parameter_source(resource.name);
                const auto* location = workspace_->catalog().index.find(source.asset_id);
                ImGui::TextDisabled("Inherited: %s", location ? location->path.utf8().c_str() : "Shader default");
            }
            ImGui::PopID();
        }
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
        auto& session = edit_session();
        if (modal_pending())
        {
            if (session.dirty() || session.gesturing() || pending_save_failed_) ImGui::OpenPopup("Unsaved Material");
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
            if (session.is_instance())
            {
                const auto& reference = session.instance_data()->parent;
                const auto* parent = workspace_->catalog().index.find(reference.asset_id);
                ImGui::BeginDisabled(!session.writable() || session.gesturing() || modal_pending() || (shaders_ && shaders_->busy()));
                if (ImGui::BeginCombo("Parent", parent ? parent->path.utf8().c_str() : "Missing Parent"))
                {
                    for (const auto& entry : workspace_->catalog().entries)
                    {
                        if (!is_material_asset_type(entry.file.root_type) || entry.file.asset_id == session.id()) continue;
                        if (ImGui::Selectable(entry.path.utf8().c_str(), entry.file.asset_id == reference.asset_id))
                        {
                            AssetRef next;
                            next.asset_id = entry.file.asset_id;
                            next.expected_type = entry.file.root_type;
                            report(session.set_parent(next));
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                if (ImGui::Button("Open Parent")) request_open(session.instance_data()->parent.asset_id);
                ImGui::SameLine();
                if (ImGui::Button("Locate Parent")) locate_parent_ = session.instance_data()->parent.asset_id;
                if (ImGui::CollapsingHeader("Parent Chain"))
                    for (auto layer = session.parent_layers().rbegin(); layer != session.parent_layers().rend(); ++layer)
                    {
                        const auto* location = workspace_->catalog().index.find(layer->reference.asset_id);
                        ImGui::TextUnformatted(location ? location->path.utf8().c_str() : layer->reference.asset_id.hex().c_str());
                    }
            }
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
            if (!error_.empty())
            {
                ImGui::TextWrapped("%s", error_.c_str());
                if (!session.dirty() && ImGui::Button("Retry Publish")) report(session.publish_saved());
            }
            ImGui::Separator();
            if (focused_ && ImGui::IsKeyPressed(ImGuiKey_Escape) && session.gesturing())
            {
                report(session.cancel_gesture());
                ImGui::ClearActiveID();
            }
            draw_parameters();

        }
        else if (session.gesturing()) report(session.cancel_gesture());
        ImGui::End();
    }
}
