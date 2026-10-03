#include "assets/material/material_editor_panel.h"
#include "assets/preview/preview_scene_widgets.h"

#include <algorithm>
#include <exception>
#include <cmath>
#include <utility>

#include "imgui.h"
#include "panels/property_widgets.h"
#include "imgui_internal.h"

#include "asset/texture/builtin_texture_assets.h"
#include "assets/asset_resource_picker.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "drivers/rhi/rhi_resource.h"

#include "logging/logger.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/texture/texture_asset_loader.h"
#include "workspace/editor_workspace.h"
#include "shader/shader_workflow.h"

namespace toy3d
{
    namespace
    {
        Sha256Hash static_options_identity(const MaterialEditSession& session)
        {
            MaterialAssetData data;
            data.shader_name = session.root_data().shader_name;
            data.static_options = session.effective_static_options();
            ValueWriter writer;
            return encode_value(writer, data).succeeded() ? sha256(writer.bytes()) : Sha256Hash{};
        }

        AssetStatus parameter_error(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, {}, {}, message, {}};
        }

        const MaterialParameterOverride* find_override(const std::vector<MaterialParameterOverride>& values,
                                                       const std::string& name)
        {
            for (const auto& value : values)
            {
                if (value.name == name)
                {
                    return &value;
                }
            }
            return nullptr;
        }

        MaterialParameterOverride constant_default(const MaterialDesc& desc,
                                                   const shader::ShaderParameterConstantMemberSchema& member)
        {
            MaterialParameterOverride value;
            value.name = member.name;
            if (member.type == shader::ShaderValueType::Float32)
            {
                value.value = desc.scalar_defaults.at(member.parameter_id);
            }
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
    } // namespace

    void MaterialEditorPanel::set_shader_workflow(ShaderWorkflow& workflow)
    {
        shaders_ = &workflow;
        if (workspace_)
        {
            workflow.set_material_workspace(*workspace_,
                                            [this](const std::string& name)
                                            {
                                                const auto& session = edit_session();
                                                return session.active() && session.root_data().shader_name == name
                                                           ? material_static_selections(
                                                                 session.effective_static_options())
                                                           : std::vector<shader::ShaderPermutationSelection>{};
                                            });
        }
    }

    void MaterialEditorPanel::initialize(EditorWorkspace& workspace, MaterialRef defaults,
                                         const PhysicalPath& shader_root)
    {
        workspace_ = &workspace;
        session_ = std::make_unique<MaterialEditSession>(workspace);
        defaults_ = std::move(defaults);
        textures_.named_defaults.clear();
        if (defaults_)
        {
            for (const auto& resource : defaults_->parameter_schema().resources)
            {
                const auto found = defaults_->desc().texture_defaults.find(resource.parameter_id);
                if (found != defaults_->desc().texture_defaults.end())
                {
                    textures_.named_defaults[resource.default_value] = found->second;
                }
            }
        }
        if (defaults_ && defaults_->desc().shader_map &&
            !workspace.read_material_properties(
                shader_root, defaults_->desc().shader_name,
                defaults_->desc()
                    .shader_map->find(shader::ShaderPassRole::Forward, shader::VertexFactoryType::Local)
                    .program->data()
                    .parameter_schema,
                properties_, metadata_warning_))
        {
            TOY_LOG_WARN("Material UI properties ignored: {}", metadata_warning_);
        }
    }

    void MaterialEditorPanel::report(const AssetStatus& status)
    {
        if (status.succeeded())
        {
            error_.clear();
            return;
        }
        error_ = status.message;
        TOY_LOG_ERROR("Material edit [{} {}]: {}", status.asset_id.hex(), status.virtual_path, status.message);
    }

    void MaterialEditorPanel::request_open(const AssetId& id)
    {
        if (!workspace_)
        {
            return;
        }
        if (edit_session().active() && edit_session().id() == id)
        {
            focus_requested_ = true;
            return;
        }
        requested_ = id;
        close_requested_ = false;
        exit_requested_ = false;
    }

    AssetStatus MaterialEditorPanel::ensure_texture_values(const std::vector<MaterialParameterOverride>& values)
    {
        for (const auto& item : values)
        {
            // C++17 get_if keeps asset-backed texture loading at the GT edge.
            const auto* reference = std::get_if<AssetRef>(&item.value);
            if (!reference || textures_.assets.count(reference->asset_id))
            {
                continue;
            }
            const auto loaded = load_texture_asset(workspace_->files(), workspace_->catalog().index, *reference);
            if (!loaded.succeeded())
            {
                return loaded.status();
            }
            textures_.assets.emplace(reference->asset_id, loaded.value());
        }
        return AssetStatus::success();
    }
    void MaterialEditorPanel::request_close()
    {
        close_requested_ = true;
        requested_ = {};
    }
    bool MaterialEditorPanel::request_exit()
    {
        if (!workspace_ || (!edit_session().dirty() && !edit_session().gesturing()))
        {
            return true;
        }
        exit_requested_ = true;
        close_requested_ = true;
        requested_ = {};
        return false;
    }
    bool MaterialEditorPanel::take_exit()
    {
        const bool ready = exit_ready_;
        exit_ready_ = false;
        return ready;
    }

    bool MaterialEditorPanel::resolve_unsaved(MaterialCloseDecision decision)
    {
        if (!workspace_ || !modal_pending())
        {
            return false;
        }
        if (decision == MaterialCloseDecision::Cancel)
        {
            requested_ = {};
            close_requested_ = false;
            exit_requested_ = false;
            pending_save_failed_ = false;
            return true;
        }
        if (decision == MaterialCloseDecision::Save)
        {
            const auto ready = validate_static_preview();
            const auto saved = ready.succeeded() ? edit_session().save() : ready;
            report(saved);
            if (!saved.succeeded())
            {
                // Keep the reported failure visible even when the file reached
                // its clean checkpoint but catalog/render publication failed.
                pending_save_failed_ = true;
                return false;
            }
        }
        else
        {
            close();
        }
        complete_transition();
        return true;
    }

    MaterialParameterChanges MaterialEditorPanel::parameter_changes(
        const std::vector<MaterialParameterOverride>& effective) const
    {
        MaterialParameterChanges result;
        const auto& schema = defaults_->parameter_schema();
        for (const auto& buffer : schema.constant_buffers)
        {
            for (const auto& member : buffer.members)
            {
                MaterialParameterChange change;
                change.name = member.name;
                const auto* override_value = find_override(effective, member.name);
                if (override_value)
                {
                    // C++17 get_if maps persisted values into the runtime's closed
                    // value set without coupling RenderCore to editor snapshots.
                    if (const auto* value = std::get_if<float>(&override_value->value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<Vector2>(&override_value->value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<Vector3>(&override_value->value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<Vector4>(&override_value->value))
                    {
                        change.value = *value;
                    }
                }
                result.push_back(std::move(change));
            }
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
                    if (found != textures_.assets.end())
                    {
                        change.value = found->second;
                    }
                }
                else if (const auto* sampler = std::get_if<MaterialSamplerPreset>(&override_value->value))
                {
                    change.value = *sampler;
                }
            }
            result.push_back(std::move(change));
        }
        return result;
    }

    AssetResult<MaterialInstanceRef> MaterialEditorPanel::build_preview_material(const MaterialAssetData& data,
                                                                                 ShaderMapCollectionRef program)
    {
        if (workspace_ && program)
        {
            const auto defaults = resolve_builtin_material_texture_defaults(
                workspace_->files(), workspace_->catalog().index, program->programs().front()->data().parameter_schema,
                textures_);
            if (!defaults.succeeded())
            {
                return AssetResult<MaterialInstanceRef>(defaults);
            }
        }
        return create_material_from_asset(data, std::move(program), textures_);
    }

    bool MaterialEditorPanel::open(const AssetId& id)
    {
        if (!defaults_ || !defaults_->desc().shader_map)
        {
            report(parameter_error("The registered Phong Shader is unavailable."));
            return false;
        }
        ShaderMapCollectionRef program = defaults_->desc().shader_map;
        if (shaders_)
        {
            const auto* location = workspace_->catalog().index.find(id);
            if (!location)
            {
                report(parameter_error("Material asset is missing."));
                return false;
            }
            AssetRef reference;
            reference.asset_id = id;
            reference.expected_type = location->index.root_type;
            const auto hierarchy = read_material_hierarchy(workspace_->types(), workspace_->files(),
                                                           workspace_->catalog().index, reference);
            if (!hierarchy.succeeded())
            {
                report(hierarchy.status());
                return false;
            }
            const auto& root = hierarchy.value().root;
            program = shaders_->shader_map(root.shader_name,
                                           material_static_selections(hierarchy.value().effective_static_options()));
            if (!program)
            {
                report(parameter_error("Shader has no published Program. Compile it from Create Material first."));
                return false;
            }
        }
        const auto schema =
            material_parameter_schema_from_shader_schema(program->programs().front()->data().parameter_schema);
        MaterialEditSession candidate(*workspace_);
        auto status = candidate.open(id, schema, program->index().shader_name, program->index().material_domain);
        if (!status.succeeded())
        {
            report(status);
            return false;
        }
        // Build the complete effective root first; the runtime object is private
        // to this window and does not mutate ActorFactory's shared default.
        MaterialAssetData effective = candidate.root_data();
        effective.overrides = candidate.effective_overrides();
        effective.static_options = candidate.effective_static_options();
        status = ensure_texture_values(effective.overrides);
        if (!status.succeeded())
        {
            report(status);
            return false;
        }
        MaterialInstanceRef next;
        {
            const auto built = build_preview_material(effective, program);
            if (!built.succeeded())
            {
                report(built.status());
                return false;
            }
            next = built.value();
        }
        status = edit_session().open(id, schema, program->index().shader_name, program->index().material_domain);
        if (!status.succeeded())
        {
            MaterialInstance::release(next);
            report(status);
            return false;
        }
        if (runtime_)
        {
            if (previews_)
            {
                previews_->clear_material_preview();
            }
            MaterialInstance::release(runtime_);
        }
        ++preview_revision_;
        runtime_ = std::move(next);
        defaults_ = runtime_->material();
        ++session_revision_;
        if (shaders_)
        {
            const auto* source = shaders_->find(program->index().shader_name);
            if (source)
            {
                properties_ = source->properties;
            }
        }
        edit_session().set_preview(
            [this](const std::vector<MaterialParameterOverride>& values)
            {
                const auto textures = ensure_texture_values(values);
                if (!textures.succeeded())
                {
                    return textures;
                }
                return runtime_->validate_parameters(parameter_changes(values))
                           ? AssetStatus::success()
                           : parameter_error("The complete parameter batch could not be prepared.");
            },
            [this](const std::vector<MaterialParameterOverride>& values)
            {
                try
                {
                    if (!runtime_->apply_parameters(parameter_changes(values)))
                    {
                        report(parameter_error("Material parameters could not be published."));
                    }
                    else
                    {
                        ++preview_revision_;
                    }
                }
                catch (const std::exception& exception)
                {
                    report(parameter_error(exception.what()));
                }
            });
        edit_session().set_static_domain_resolver(
            [this](const std::string& name) -> AssetResult<shader::ShaderPermutationDomain>
            {
                const auto program = shaders_ ? shaders_->shader_map(name) : defaults_->desc().shader_map;
                if (!program || program->index().shader_name != name)
                {
                    return AssetResult<shader::ShaderPermutationDomain>(
                        parameter_error("Compile the Parent Shader before selecting it."));
                }
                return AssetResult<shader::ShaderPermutationDomain>(program->index().material_domain);
            });
        edit_session().set_parent_preview(
            [this](const std::string& name) -> AssetResult<shader::ShaderParameterSchema>
            {
                const auto program =
                    shaders_ ? shaders_->shader_map(name)
                             : (defaults_->desc().shader_name == name ? defaults_->desc().shader_map : nullptr);
                if (!program)
                {
                    return AssetResult<shader::ShaderParameterSchema>(
                        parameter_error("Compile the Parent Shader before selecting it."));
                }
                return AssetResult<shader::ShaderParameterSchema>(
                    material_parameter_schema_from_shader_schema(program->programs().front()->data().parameter_schema));
            },
            [this](const MaterialAssetData& effective)
            {
                discard_shader();
                const auto textures = ensure_texture_values(effective.overrides);
                if (!textures.succeeded())
                {
                    return textures;
                }
                const auto program = shaders_
                                         ? shaders_->shader_map(effective.shader_name,
                                                                material_static_selections(effective.static_options))
                                         : defaults_->desc().shader_map;
                const auto built = build_preview_material(effective, program);
                if (!built.succeeded())
                {
                    return built.status();
                }
                shader_candidate_ = built.value();
                candidate_schema_ = shader_candidate_->parameter_schema();
                candidate_static_domain_ = program->index().material_domain;
                if (shaders_)
                {
                    const auto* source = shaders_->find(effective.shader_name);
                    if (source)
                    {
                        candidate_properties_ = source->properties;
                    }
                }
                return AssetStatus::success();
            },
            [this]()
            {
                publish_shader();
                ++session_revision_;
            });
        error_.clear();
        focus_requested_ = true;
        return true;
    }

    void MaterialEditorPanel::close()
    {
        discard_shader();
        ++session_revision_;
        static_recompile_pending_ = false;
        edit_session().clear();
        if (runtime_)
        {
            if (previews_)
            {
                previews_->clear_material_preview();
            }
            MaterialInstance::release(runtime_);
        }
        focused_ = false;
    }

    bool MaterialEditorPanel::prepare_shader(const ShaderMapCollectionRef& program,
                                             const std::vector<shader::ShaderEditorProperty>& properties,
                                             std::string& error)
    {
        return prepare_shader(std::vector<ShaderMapCollectionRef>{program}, properties, error);
    }

    bool MaterialEditorPanel::prepare_shader(const std::vector<ShaderMapCollectionRef>& programs,
                                             const std::vector<shader::ShaderEditorProperty>& properties,
                                             std::string& error)
    {
        discard_shader();
        auto& session = edit_session();
        if (programs.empty() || !programs.front())
        {
            error = "Material Shader candidate configuration set is empty.";
            return false;
        }
        if (!session.active() || session.root_data().shader_name != programs.front()->index().shader_name)
        {
            return true;
        }
        const auto options = session.effective_static_options();
        const auto selection = shader::resolve_shader_permutation(programs.front()->index().material_domain,
                                                                  material_static_selections(options));
        if (!selection.succeeded())
        {
            error = selection.errors.front().message;
            return false;
        }
        const auto selected = std::find_if(programs.begin(), programs.end(),
                                           [&](const ShaderMapCollectionRef& configuration)
                                           {
                                               return configuration && configuration->index().permutation_key ==
                                                                           selection.permutation->key;
                                           });
        if (selected == programs.end())
        {
            error = "Shader candidate is missing the current material draft configuration.";
            return false;
        }
        const auto& program = *selected;
        if (session.gesturing())
        {
            error = "Finish the parameter gesture before applying compiled code.";
            return false;
        }
        MaterialAssetData effective = session.root_data();
        effective.static_options = options;
        auto schema =
            material_parameter_schema_from_shader_schema(program->programs().front()->data().parameter_schema);
        effective.overrides = session.effective_overrides(schema);
        const auto textures = ensure_texture_values(effective.overrides);
        if (!textures.succeeded())
        {
            error = textures.message;
            return false;
        }
        candidate_properties_ = properties;
        candidate_schema_ = std::move(schema);
        candidate_static_domain_ = program->index().material_domain;
        const auto built = build_preview_material(effective, program);
        if (!built.succeeded())
        {
            error = built.status().message;
            return false;
        }
        shader_candidate_ = built.value();
        return true;
    }

    void MaterialEditorPanel::publish_shader()
    {
        if (!shader_candidate_)
        {
            return;
        }
        auto& session = edit_session();
        auto status = session.update_static_domain(std::move(candidate_static_domain_));
        if (status.succeeded())
        {
            status = session.update_schema(std::move(candidate_schema_));
        }
        if (!status.succeeded())
        {
            report(status);
            discard_shader();
            return;
        }
        if (runtime_)
        {
            if (previews_)
            {
                previews_->clear_material_preview();
            }
            MaterialInstance::release(runtime_);
        }
        ++preview_revision_;
        runtime_ = std::move(shader_candidate_);
        defaults_ = runtime_->material();
        properties_ = std::move(candidate_properties_);
        metadata_warning_.clear();
        error_.clear();
    }
    void MaterialEditorPanel::discard_shader()
    {
        if (shader_candidate_)
        {
            MaterialInstance::release(shader_candidate_);
        }
        candidate_properties_.clear();
        candidate_schema_ = {};
        candidate_static_domain_ = {};
    }
    bool MaterialEditorPanel::collect_shader_validation_targets(const std::vector<ShaderMapCollectionRef>& programs,
                                                                std::vector<MaterialShaderMapValidationTarget>& targets,
                                                                std::string& error) const
    {
        if (!runtime_ || !edit_session().active() || programs.empty() ||
            edit_session().root_data().shader_name != programs.front()->index().shader_name)
        {
            return true;
        }
        const auto selected =
            shader::resolve_shader_permutation(programs.front()->index().material_domain,
                                               material_static_selections(edit_session().effective_static_options()));
        if (!selected.succeeded())
        {
            error = selected.errors.front().message;
            return false;
        }
        const auto found = std::find_if(programs.begin(), programs.end(),
                                        [&](const ShaderMapCollectionRef& value)
                                        {
                                            return value->index().permutation_key == selected.permutation->key;
                                        });
        if (found == programs.end())
        {
            error = "Material preview draft has no candidate configuration.";
            return false;
        }
        targets.push_back({runtime_->material_render_proxy(), *found});
        return true;
    }
    void MaterialEditorPanel::complete_transition()
    {
        if (requested_.valid())
        {
            open(requested_);
        }
        else if (close_requested_)
        {
            close();
        }
        if (exit_requested_)
        {
            exit_ready_ = true;
        }
        requested_ = {};
        close_requested_ = false;
        exit_requested_ = false;
        pending_save_failed_ = false;
    }
    void MaterialEditorPanel::shutdown()
    {
        if (session_)
        {
            session_->set_publish({});
        }
        if (workspace_)
        {
            close();
        }
        defaults_.reset();
        textures_.named_defaults.clear();
        textures_.assets.clear();
        workspace_ = nullptr;
        previews_ = nullptr;
        resource_picker_ = nullptr;
        shaders_ = nullptr;
        session_.reset();
    }
    void MaterialEditorPanel::request_static_configuration()
    {
        ++session_revision_;
        static_recompile_pending_ = shaders_ && shaders_->busy();
        if (shaders_ && !shaders_->busy() && !modal_pending() && !edit_session().gesturing())
        {
            shaders_->recompile(edit_session().root_data().shader_name, edit_session().id(), session_revision_);
        }
    }

    void MaterialEditorPanel::set_static_option(const MaterialStaticOption& value)
    {
        const auto before = static_options_identity(edit_session());
        const auto status = edit_session().set_static_option(value);
        report(status);
        if (status.succeeded() && before != static_options_identity(edit_session()))
        {
            request_static_configuration();
        }
    }

    void MaterialEditorPanel::remove_static_option(const std::string& name)
    {
        const auto before = static_options_identity(edit_session());
        const auto status = edit_session().remove_static_option(name);
        report(status);
        if (status.succeeded() && before != static_options_identity(edit_session()))
        {
            request_static_configuration();
        }
    }

    void MaterialEditorPanel::undo()
    {
        if (workspace_ && edit_session().undo_count())
        {
            const auto before = static_options_identity(edit_session());
            const auto status = edit_session().undo();
            report(status);
            if (status.succeeded() && before != static_options_identity(edit_session()))
            {
                request_static_configuration();
            }
        }
    }

    void MaterialEditorPanel::redo()
    {
        if (workspace_ && edit_session().redo_count())
        {
            const auto before = static_options_identity(edit_session());
            const auto status = edit_session().redo();
            report(status);
            if (status.succeeded() && before != static_options_identity(edit_session()))
            {
                request_static_configuration();
            }
        }
    }
    AssetStatus MaterialEditorPanel::validate_static_preview() const
    {
        const auto& session = edit_session();
        if (!session.active() || !runtime_ || !runtime_->desc().shader_map)
        {
            return parameter_error("The material preview is unavailable.");
        }
        const auto selected = shader::resolve_shader_permutation(
            session.static_domain(), material_static_selections(session.effective_static_options()));
        return selected.succeeded() && selected.permutation->key == runtime_->desc().shader_map->index().permutation_key
                   ? AssetStatus::success()
                   : parameter_error("Compile and apply the current static options before saving.");
    }

    void MaterialEditorPanel::save()
    {
        if (workspace_ && edit_session().active())
        {
            const auto ready = validate_static_preview();
            report(ready.succeeded() ? edit_session().save() : ready);
        }
    }

    void MaterialEditorPanel::draw_static_options()
    {
        auto& session = edit_session();
        const auto& domain = session.static_domain();
        const auto options = session.effective_static_options();
        const auto configuration = shader::resolve_shader_permutation(domain, material_static_selections(options));
        if (!configuration.succeeded())
        {
            ImGui::TextWrapped("Static options unavailable: %s", configuration.errors.front().message.c_str());
            return;
        }
        if (!domain.dimensions.empty() && ImGui::CollapsingHeader("Static Options", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto local = session.static_options();
            for (const auto& dimension : domain.dimensions)
            {
                const auto chosen = std::find_if(configuration.permutation->selections.begin(),
                                                 configuration.permutation->selections.end(),
                                                 [&](const shader::ShaderPermutationSelection& item)
                                                 {
                                                     return item.name == dimension.name;
                                                 });
                const bool overridden = std::any_of(local.begin(), local.end(),
                                                    [&](const MaterialStaticOption& item)
                                                    {
                                                        return item.name == dimension.name;
                                                    });
                ImGui::PushID(dimension.name.c_str());
                ImGui::BeginDisabled(!session.writable() || session.gesturing() || modal_pending() ||
                                     (shaders_ && shaders_->busy()));
                if (!begin_property_row(dimension.name.c_str(),
                                        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x))
                {
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    continue;
                }
                if (dimension.kind == shader::ShaderPermutationValueKind::Boolean)
                {
                    bool value = chosen->boolean_value;
                    if (ImGui::Checkbox("##Value", &value))
                    {
                        set_static_option({dimension.name, value});
                    }
                }
                else
                {
                    if (ImGui::BeginCombo("##Value", chosen->enum_value.c_str()))
                    {
                        for (const auto& option : dimension.options)
                        {
                            if (ImGui::Selectable(option.c_str(), chosen->enum_value == option))
                            {
                                set_static_option({dimension.name, option});
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!overridden);
                if (property_action_button("Reset", PropertyAction::Reset,
                                           session.is_instance() ? "Restore inherited option"
                                                                 : "Restore default option"))
                {
                    remove_static_option(dimension.name);
                }
                ImGui::EndDisabled();
                end_property_row();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            if (runtime_ && runtime_->desc().shader_map &&
                runtime_->desc().shader_map->index().permutation_key != configuration.permutation->key)
            {
                ImGui::TextWrapped("Static options are pending compilation. The previous material remains visible.");
            }
        }
        if (shaders_ && ImGui::CollapsingHeader("Shader Capabilities"))
        {
            const auto* source = shaders_->find(session.root_data().shader_name);
            if (source && source->shader_map)
            {
                const auto& index = source->shader_map->index();
                shader::ShaderCompileSource declaration;
                declaration.material_domain = index.material_domain;
                declaration.features = index.features;
                shader::ShaderEngineFeatures features;
                std::string error;
                if (shader::resolve_shader_engine_features(declaration, configuration.permutation->selections,
                                                           index.policy, features, error))
                {
                    ImGui::TextDisabled("Lighting: %s | Shadows: %s | Environment: %s",
                                        features.lighting ? "Yes" : "No", features.shadows ? "Yes" : "No",
                                        features.environment ? "Yes" : "No");
                }
                else
                {
                    ImGui::TextWrapped("%s", error.c_str());
                }
                const auto factories = index.programs.front().contract.vertex_factory_support;
                ImGui::TextDisabled(
                    "Local: %s | GPU Skin: %s",
                    shader::supports_vertex_factory(factories, shader::VertexFactoryType::Local) ? "Yes" : "No",
                    shader::supports_vertex_factory(factories, shader::VertexFactoryType::GPUSkin) ? "Yes" : "No");
            }
        }
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
        {
            for (const auto& member : buffer.members)
            {
                ParameterRow row{&member, nullptr};
                for (const auto& property : properties_)
                {
                    if (property.name == member.name)
                    {
                        row.property = &property;
                    }
                }
                rows.push_back(row);
            }
        }
        std::stable_sort(rows.begin(), rows.end(),
                         [](const ParameterRow& a, const ParameterRow& b)
                         {
                             if (a.property && b.property)
                             {
                                 return a.property->display_order < b.property->display_order;
                             }
                             if (a.property != nullptr || b.property != nullptr)
                             {
                                 return a.property != nullptr;
                             }
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
            const char* title = row.property ? row.property->display_name.c_str() : member.name.c_str();
            const float action_width = ImGui::GetFrameHeight();
            if (!begin_property_row(title))
            {
                ImGui::PopID();
                continue;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(title);
                if (!overridden)
                {
                    const auto source = session.parameter_source(member.name);
                    const auto* location = workspace_->catalog().index.find(source.asset_id);
                    ImGui::Text("Inherited: %s", location ? location->path.utf8().c_str() : "Shader default");
                }
                const auto& programs = defaults_->desc().shader_map->programs();
                if (std::none_of(programs.begin(), programs.end(),
                                 [&member](const ShaderMapProgramRef& program)
                                 {
                                     return program->find_parameter_binding(member.parameter_id) != nullptr;
                                 }))
                {
                    ImGui::TextUnformatted("Unused in this variant");
                }
                ImGui::EndTooltip();
            }
            const float value_width = std::max(1.0f, ImGui::GetContentRegionAvail().x - action_width * 2.0f -
                                                         ImGui::GetStyle().ItemSpacing.x * 2.0f);
            ImGui::BeginDisabled(!session.writable() || session.gesturing());
            if (ImGui::Checkbox("##Override", &overridden))
            {
                report(overridden ? session.set_parameter(value) : session.remove_parameter(member.name));
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.writable());
            ImGui::SetNextItemWidth(value_width);
            bool changed = false;
            // C++17 get_if selects familiar ImGui controls for the fixed numeric
            // alternatives. Shader UI metadata supplies presentation only.
            if (auto* scalar = std::get_if<float>(&value.value))
            {
                if (row.property && row.property->control == shader::ShaderEditorPropertyControl::Range &&
                    row.property->range_min && row.property->range_max)
                {
                    changed = ImGui::SliderFloat("##Value", scalar, *row.property->range_min, *row.property->range_max);
                }
                else
                {
                    changed = ImGui::DragFloat("##Value", scalar, 0.01f);
                }
                if (changed && row.property)
                {
                    if (row.property->range_min)
                    {
                        *scalar = std::max(*scalar, *row.property->range_min);
                    }
                    if (row.property->range_max)
                    {
                        *scalar = std::min(*scalar, *row.property->range_max);
                    }
                }
            }
            else if (auto* vector = std::get_if<Vector2>(&value.value))
            {
                changed = ImGui::DragFloat2("##Value", vector->data(), 0.01f);
            }
            else if (auto* vector = std::get_if<Vector3>(&value.value))
            {
                changed = ImGui::DragFloat3("##Value", vector->data(), 0.01f);
            }
            else if (auto* vector = std::get_if<Vector4>(&value.value))
            {
                if (row.property && row.property->control == shader::ShaderEditorPropertyControl::Color)
                {
                    changed = property_color_value("##Value", vector->data(), true, value_width);
                }
                else
                {
                    changed = ImGui::DragFloat4("##Value", vector->data(), 0.01f);
                }
            }
            if (ImGui::IsItemActivated() && !session.gesturing())
            {
                report(session.begin_gesture());
            }
            if (changed)
            {
                report(session.set_parameter(value));
            }
            if (ImGui::IsItemDeactivated() && session.gesturing())
            {
                report(session.finish_gesture());
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!overridden || session.gesturing());
            if (property_action_button("Reset", PropertyAction::Reset,
                                       session.is_instance() ? "Restore inherited value" : "Restore Shader default"))
            {
                report(session.remove_parameter(member.name));
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            end_property_row();
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
            {
                if (property.name == resource.name)
                {
                    title = property.display_name.c_str();
                    break;
                }
            }
            const bool numeric_resource = resource.resource_kind != shader::ResourceKind::Texture2D;
            if (numeric_resource &&
                !begin_property_row(title, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x))
            {
                ImGui::PopID();
                continue;
            }
            ImGui::BeginDisabled(!session.writable() || session.gesturing());
            if (resource.category == shader::ShaderParameterCategory::SampledTexture &&
                resource.resource_kind == shader::ResourceKind::Texture2D)
            {
                // C++17 get_if distinguishes texture references from sampler presets without coercion.
                const auto* reference = selected ? std::get_if<AssetRef>(&selected->value) : nullptr;
                AssetResourceSelection current;
                if (reference)
                {
                    current.asset = reference->asset_id;
                }
                else
                {
                    for (const auto& builtin : builtin_texture_assets)
                    {
                        if (resource.default_value == builtin.default_name)
                        {
                            AssetId::parse(builtin.asset_id, current.asset);
                            break;
                        }
                    }
                }
                if (resource_picker_)
                {
                    AssetResourceSelection next;
                    std::string error;
                    if (resource_picker_->draw(title, *workspace_, current, {"toy3d.Texture2DAssetData"}, next, error))
                    {
                        if (!next.asset.valid())
                        {
                            report(session.remove_parameter(resource.name));
                        }
                        else
                        {
                            AssetRef asset;
                            asset.asset_id = next.asset;
                            asset.expected_type = "toy3d.Texture2DAssetData";
                            MaterialParameterOverride value{resource.name, asset};
                            const auto ready = ensure_texture_values({value});
                            report(ready.succeeded() ? session.set_parameter(value) : ready);
                        }
                    }
                    if (!error.empty())
                    {
                        error_ = error;
                    }
                }
                else
                {
                    ImGui::TextDisabled("Texture picker unavailable");
                }
            }
            else if (resource.category == shader::ShaderParameterCategory::Sampler &&
                     resource.resource_kind == shader::ResourceKind::Sampler)
            {
                MaterialSamplerPreset preset{};
                if (selected)
                {
                    const auto* chosen = std::get_if<MaterialSamplerPreset>(&selected->value);
                    if (chosen)
                    {
                        preset = *chosen;
                    }
                }
                else
                {
                    parse_material_sampler_preset(resource.default_value, preset);
                }
                const auto ordinal = static_cast<std::size_t>(preset);
                const char* label = ordinal < shader::sampler_preset_count - 1u
                                        ? shader::sampler_preset_name(static_cast<std::uint32_t>(ordinal))
                                        : "Unsupported";
                if (ImGui::BeginCombo("##Sampler", label))
                {
                    for (std::uint32_t i = 0; i < shader::sampler_preset_count - 1u; ++i)
                    {
                        if (ImGui::Selectable(shader::sampler_preset_name(i), ordinal == i))
                        {
                            report(session.set_parameter({resource.name, static_cast<MaterialSamplerPreset>(i)}));
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::TextDisabled("Unsupported resource type");
            }
            if (numeric_resource)
            {
                ImGui::SameLine();
                ImGui::BeginDisabled(!overridden);
                if (property_action_button("Reset", PropertyAction::Reset,
                                           session.is_instance() ? "Restore inherited value"
                                                                 : "Restore Shader default"))
                {
                    report(session.remove_parameter(resource.name));
                }
                ImGui::EndDisabled();
            }
            ImGui::EndDisabled();
            if (numeric_resource)
            {
                end_property_row();
            }
            ImGui::PopID();
        }
        const auto snapshot = session.overrides();
        for (const auto& value : snapshot)
        {
            if (material_override_matches_schema(value, session.schema()))
            {
                continue;
            }
            ImGui::PushID(value.name.c_str());
            ImGui::TextWrapped("Orphan: %s (parameter missing or type changed; preserved on save)", value.name.c_str());
            ImGui::BeginDisabled(!session.writable() || session.gesturing());
            if (ImGui::Button("Remove orphan"))
            {
                report(session.remove_parameter(value.name));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    void MaterialEditorPanel::draw_preview()
    {
        const auto shape_button = [&](const char* label, MaterialPreviewMesh mesh)
        {
            const bool active = preview_settings_.mesh == mesh;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(label))
            {
                preview_settings_.mesh = mesh;
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
        };
        shape_button("Sphere", MaterialPreviewMesh::Sphere);
        ImGui::SameLine();
        shape_button("Plane", MaterialPreviewMesh::Plane);
        ImGui::SameLine();
        shape_button("Cube", MaterialPreviewMesh::Cube);
        ImGui::Separator();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size(std::max(96.0f, std::min(available.x, 1024.0f)),
                          std::max(96.0f, std::min(available.y - 40.0f, 1024.0f)));
        // Readback is bounded by the public RHI; a larger pane scales the image rather than the GPU target.
        const float scale = std::min(1.0f, rhi_max_texture_readback_dimension / std::max(size.x, size.y));
        preview_settings_.extent = {std::max(96u, static_cast<std::uint32_t>(size.x * scale / 16.0f) * 16u),
                                    std::max(96u, static_cast<std::uint32_t>(size.y * scale / 16.0f) * 16u)};
        const float fit = std::min(size.x / preview_settings_.extent.width, size.y / preview_settings_.extent.height);
        const ImVec2 image_size(preview_settings_.extent.width * fit, preview_settings_.extent.height * fit);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (size.x - image_size.x) * 0.5f);
        const auto preview = previews_->request_material_preview(runtime_, preview_revision_, preview_settings_);
        if (preview.texture_id.valid())
        {
            const auto position = ImGui::GetCursorScreenPos();
            // Claim orbit gestures so the root window cannot move when dragging its preview image.
            ImGui::InvisibleButton("PreviewImage", image_size);
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(preview.texture_id.value())), position,
                ImVec2(position.x + image_size.x, position.y + image_size.y));
            if (ImGui::IsItemHovered())
            {
                const auto& io = ImGui::GetIO();
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    preview_settings_.camera_yaw =
                        std::fmod(preview_settings_.camera_yaw - io.MouseDelta.x * 0.5f, 360.0f);
                    preview_settings_.camera_pitch =
                        std::max(-80.0f, std::min(80.0f, preview_settings_.camera_pitch + io.MouseDelta.y * 0.5f));
                }
                preview_settings_.camera_distance =
                    std::max(220.0f, std::min(1000.0f, preview_settings_.camera_distance - io.MouseWheel * 30.0f));
                ImGui::SetTooltip("Drag to orbit; mouse wheel to zoom");
            }
        }
        else
        {
            ImGui::Dummy(image_size);
        }
        if (preview.busy)
        {
            ImGui::TextDisabled("Updating preview...");
        }
        if (!preview.error.empty())
        {
            ImGui::TextWrapped("%s", preview.error.c_str());
        }
    }

    void MaterialEditorPanel::draw()
    {
        if (!workspace_)
        {
            return;
        }
        auto& session = edit_session();
        if (static_recompile_pending_ && session.active() && shaders_ && !shaders_->busy() && !modal_pending() &&
            !session.gesturing())
        {
            static_recompile_pending_ = false;
            shaders_->recompile(session.root_data().shader_name, session.id(), session_revision_);
        }
        if (modal_pending())
        {
            if (session.dirty() || session.gesturing() || pending_save_failed_)
            {
                ImGui::OpenPopup("Unsaved Material");
            }
            else
            {
                complete_transition();
            }
        }
        if (ImGui::BeginPopupModal("Unsaved Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Save changes to %s?", session.path().utf8().c_str());
            if (!error_.empty())
            {
                ImGui::TextWrapped("%s", error_.c_str());
            }
            if (ImGui::Button("Save"))
            {
                if (resolve_unsaved(MaterialCloseDecision::Save))
                {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                // Drop the entire session before switching; no extra undo stack
                // or write is performed for the discarded draft.
                if (resolve_unsaved(MaterialCloseDecision::Discard))
                {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                if (resolve_unsaved(MaterialCloseDecision::Cancel))
                {
                    ImGui::CloseCurrentPopup();
                }
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
                if (ImGui::Button("Dismiss"))
                {
                    error_.clear();
                }
                ImGui::End();
            }
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(std::min(1100.0f, ImGui::GetIO().DisplaySize.x - 40.0f),
                                        std::min(760.0f, ImGui::GetIO().DisplaySize.y - 40.0f)),
                                 ImGuiCond_FirstUseEver);
        if (focus_requested_)
        {
            ImGui::SetNextWindowFocus();
            focus_requested_ = false;
        }
        bool visible = true;
        ImGui::SetNextWindowBgAlpha(1.0f);
        const bool drawn = ImGui::Begin("Material Editor", &visible,
                                        session.dirty() || session.gesturing() ? ImGuiWindowFlags_UnsavedDocument
                                                                               : ImGuiWindowFlags_None);
        focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!visible)
        {
            request_close();
        }
        if (drawn)
        {
            ImGui::BeginDisabled(!session.writable() || modal_pending());
            if (ImGui::Button("Save"))
            {
                save();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(session.undo_count() == 0 || session.gesturing());
            if (ImGui::Button("Undo"))
            {
                undo();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(session.redo_count() == 0 || session.gesturing());
            if (ImGui::Button("Redo"))
            {
                redo();
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::Separator();
            if (focused_ && ImGui::IsKeyPressed(ImGuiKey_Escape) && session.gesturing())
            {
                report(session.cancel_gesture());
                ImGui::ClearActiveID();
            }
            if (ImGui::BeginTable("MaterialWorkspace", 2, ImGuiTableFlags_Resizable, ImGui::GetContentRegionAvail()))
            {
                ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableNextColumn();
                ImGui::BeginChild("Viewport", ImVec2(0, 0), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                if (previews_ && runtime_)
                {
                    draw_preview();
                }
                else
                {
                    ImGui::TextDisabled("Preview unavailable");
                }
                ImGui::EndChild();
                ImGui::TableNextColumn();
                ImGui::BeginChild("Properties", ImVec2(0, 0));
                if (ImGui::BeginTabBar("MaterialProperties"))
                {
                    if (ImGui::BeginTabItem("Details"))
                    {
                        ImGui::BeginChild("DetailsScroll", ImVec2(0, 0));
                        ImGui::TextWrapped("%s%s", session.path().utf8().c_str(), session.dirty() ? " *" : "");
                        ImGui::TextDisabled("%s%s", session.root_data().shader_name.c_str(),
                                            session.writable() ? "" : " | Read only");
                        if (session.is_instance())
                        {
                            const auto& reference = session.instance_data()->parent;
                            ImGui::BeginDisabled(!session.writable() || session.gesturing() || modal_pending() ||
                                                 (shaders_ && shaders_->busy()));
                            AssetResourceSelection selected;
                            std::string picker_error;
                            if (resource_picker_ && resource_picker_->draw(
                                                        "Parent", *workspace_, {reference.asset_id, {}},
                                                        {"toy3d.MaterialAssetData", "toy3d.MaterialInstanceAssetData"},
                                                        selected, picker_error,
                                                        [&](const AssetCatalogEntry& entry)
                                                        {
                                                            return !(entry.file.asset_id == session.id());
                                                        },
                                                        false, false))
                            {
                                const auto* parent = workspace_->catalog().index.find(selected.asset);
                                if (parent)
                                {
                                    report(session.set_parent(
                                        {selected.asset, {}, parent->index.root_type, AssetRefStrength::Strong}));
                                }
                            }
                            if (!picker_error.empty())
                            {
                                error_ = picker_error;
                            }
                            ImGui::EndDisabled();
                            if (ImGui::Button("Open Parent"))
                            {
                                request_open(session.instance_data()->parent.asset_id);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Locate Parent"))
                            {
                                locate_parent_ = session.instance_data()->parent.asset_id;
                            }
                            if (ImGui::CollapsingHeader("Parent Chain"))
                            {
                                for (auto layer = session.parent_layers().rbegin();
                                     layer != session.parent_layers().rend(); ++layer)
                                {
                                    const auto* location = workspace_->catalog().index.find(layer->reference.asset_id);
                                    ImGui::TextUnformatted(location ? location->path.utf8().c_str()
                                                                    : layer->reference.asset_id.hex().c_str());
                                }
                            }
                        }
                        if (shaders_)
                        {
                            if (ImGui::Button("Open Source"))
                            {
                                shaders_->open_source(session.root_data().shader_name);
                            }
                            ImGui::SameLine();
                            ImGui::BeginDisabled(shaders_->busy() || session.gesturing() || modal_pending());
                            if (ImGui::Button("Recompile"))
                            {
                                shaders_->recompile(session.root_data().shader_name, session.id(), session_revision_);
                            }
                            ImGui::EndDisabled();
                            ImGui::TextWrapped("%s", shaders_->status().c_str());
                            if (!shaders_->error().empty())
                            {
                                ImGui::TextWrapped("%s", shaders_->error().c_str());
                            }
                            if (shaders_->has_error_location() && ImGui::Button("Open Error in VS Code"))
                            {
                                shaders_->open_error();
                            }
                            if (!shaders_->output().empty() && ImGui::CollapsingHeader("Compiler Output"))
                            {
                                ImGui::TextUnformatted(shaders_->output().c_str());
                            }
                        }
                        draw_static_options();
                        ImGui::TextDisabled("Two sided: %s", session.root_data().two_sided ? "Yes" : "No");
                        if (!metadata_warning_.empty())
                        {
                            ImGui::TextWrapped("Properties unavailable; using schema controls: %s",
                                               metadata_warning_.c_str());
                        }
                        if (!error_.empty())
                        {
                            ImGui::TextWrapped("%s", error_.c_str());
                            if (!session.dirty() && ImGui::Button("Retry Publish"))
                            {
                                report(session.publish_saved());
                            }
                        }
                        ImGui::Separator();
                        draw_parameters();
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Preview Scene"))
                    {
                        ImGui::BeginChild("PreviewSettingsScroll", ImVec2(0, 0));
                        draw_preview_scene_settings(*workspace_, preview_settings_.scene);
                        if (ImGui::Button("Reset Preview"))
                        {
                            preview_settings_ = MaterialPreviewSettings{};
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }
        }
        else if (session.gesturing())
        {
            report(session.cancel_gesture());
        }
        ImGui::End();
    }
} // namespace toy3d
