#include "rendercore/material/material_asset_builder.h"

#include <algorithm>
#include <exception>
#include "asset/texture/builtin_texture_assets.h"
#include "rendercore/texture/texture_asset_decode.h"

#include "rendercore/shader/shader_map.h"

namespace toy3d
{
    namespace
    {
        AssetStatus failure(const std::string& message)
        {
            return {AssetErrorCode::Value, {}, {}, "type_data", {}, message, {}};
        }

        AssetStatus check_runtime_values(const std::vector<MaterialParameterOverride>& values,
                                         const shader::ShaderParameterSchema& schema,
                                         const MaterialTextureValues& textures)
        {
            for (const auto& item : values)
            {
                // Known persisted orphans survive saves; only matching values reach runtime setters.
                if (!material_override_matches_schema(item, schema))
                {
                    continue;
                }
                if (const auto* scalar = std::get_if<float>(&item.value))
                {
                    for (const auto& buffer : schema.constant_buffers)
                    {
                        for (const auto& member : buffer.members)
                        {
                            if (member.name == item.name && !shader::validate_shader_scalar_value(member, *scalar))
                            {
                                return failure("Material scalar is outside its declared Range: " + item.name);
                            }
                        }
                    }
                }
                // C++17 get_if resolves Texture references without erasing their Asset identity.
                if (const auto* reference = std::get_if<AssetRef>(&item.value))
                {
                    const auto found = textures.assets.find(reference->asset_id);
                    if (found == textures.assets.end() || !found->second)
                    {
                        return failure("Texture asset is not loaded for parameter: " + item.name);
                    }
                }
            }
            return AssetStatus::success();
        }

        AssetResult<MaterialInstanceRef> build_instance(MaterialRef material,
                                                        const std::vector<MaterialParameterOverride>& root,
                                                        const std::vector<MaterialParameterOverride>& child,
                                                        const MaterialTextureValues& textures)
        {
            const auto& schema = material->parameter_schema();
            AssetStatus valid = check_runtime_values(root, schema, textures);
            if (valid.succeeded())
            {
                valid = check_runtime_values(child, schema, textures);
            }
            if (!valid.succeeded())
            {
                return AssetResult<MaterialInstanceRef>(valid);
            }
            std::map<std::string, const MaterialParameterOverride*> effective;
            for (const auto& item : root)
            {
                if (material_override_matches_schema(item, schema))
                {
                    effective[item.name] = &item;
                }
            }
            for (const auto& item : child)
            {
                if (material_override_matches_schema(item, schema))
                {
                    effective[item.name] = &item;
                }
            }
            MaterialInstanceRef candidate = MaterialInstance::create(std::move(material));
            if (!candidate)
            {
                return AssetResult<MaterialInstanceRef>(failure("Could not create runtime material instance."));
            }
            try
            {
                MaterialParameterChanges changes;
                for (const auto& pair : effective)
                {
                    const auto& item = *pair.second;
                    MaterialParameterChange change;
                    change.name = item.name;
                    // C++17 get_if maps the persisted closed set into one owned
                    // runtime batch; AssetRef resolution stays on the GT.
                    if (const auto* value = std::get_if<float>(&item.value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<Vector2>(&item.value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<Vector3>(&item.value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<Vector4>(&item.value))
                    {
                        change.value = *value;
                    }
                    else if (const auto* value = std::get_if<AssetRef>(&item.value))
                    {
                        change.value = textures.assets.at(value->asset_id);
                    }
                    else if (const auto* value = std::get_if<MaterialSamplerPreset>(&item.value))
                    {
                        change.value = *value;
                    }
                    else
                    {
                        return AssetResult<MaterialInstanceRef>(
                            failure("Unsupported material parameter: " + item.name));
                    }
                    changes.push_back(std::move(change));
                }
                if (!candidate->apply_parameters(changes))
                {
                    MaterialInstance::release(candidate);
                    return AssetResult<MaterialInstanceRef>(
                        failure("Could not apply the complete material parameter batch."));
                }
            }
            catch (const std::exception& error)
            {
                // The candidate has no external users. Its final release follows its updates in FIFO.
                MaterialInstance::release(candidate);
                return AssetResult<MaterialInstanceRef>(failure(error.what()));
            }
            return AssetResult<MaterialInstanceRef>(std::move(candidate));
        }
    } // namespace

    AssetStatus resolve_builtin_material_texture_defaults(const FileSystem& files, const AssetIndex& index,
                                                          const shader::ShaderParameterSchema& schema,
                                                          MaterialTextureValues& textures)
    {
        MaterialTextureValues candidate = textures;
        for (const auto& resource : schema.resources)
        {
            if (resource.group != shader::BindingGroup::Material ||
                resource.resource_kind != shader::ResourceKind::Texture2D ||
                resource.default_value_kind != shader::ShaderParameterDefaultValueKind::String)
            {
                continue;
            }
            const auto existing = candidate.named_defaults.find(resource.default_value);
            if (existing != candidate.named_defaults.end() && existing->second)
            {
                if (existing->second->desc().cube || existing->second->desc().usage != resource.texture_usage)
                {
                    return failure("Builtin texture default usage mismatch: " + resource.name);
                }
                continue;
            }
            const auto spec = std::find_if(builtin_texture_assets.begin(), builtin_texture_assets.end(),
                                           [&](const BuiltinTextureAsset& entry)
                                           {
                                               return resource.default_value == entry.default_name;
                                           });
            if (spec == builtin_texture_assets.end())
            {
                // Explicit project defaults remain the calling composition root's responsibility.
                continue;
            }
            AssetRef reference;
            if (!AssetId::parse(spec->asset_id, reference.asset_id) || spec->usage != resource.texture_usage)
            {
                return failure("Builtin texture default has incompatible Usage: " + resource.name);
            }
            reference.expected_type = "toy3d.Texture2DAssetData";
            const auto* location = index.find(reference.asset_id);
            if (!location || location->path.utf8().compare(0u, 8u, "/Engine/") != 0)
            {
                return failure("Missing engine texture default: " + resource.default_value);
            }
            const auto descriptor = build_texture2d_desc(files, index, reference);
            if (!descriptor.succeeded())
            {
                return descriptor.status();
            }
            TextureRef texture = Texture::create(std::move(descriptor).value());
            if (!texture)
            {
                return failure("Invalid engine texture default: " + resource.default_value);
            }
            candidate.named_defaults[resource.default_value] = std::move(texture);
        }
        textures = std::move(candidate);
        return AssetStatus::success();
    }

    AssetResult<MaterialDesc> material_descriptor_from_asset(const MaterialAssetData& data,
                                                             ShaderMapCollectionRef program,
                                                             const MaterialTextureValues& textures)
    {
        const AssetStatus valid = validate_material_asset(data);
        if (!valid.succeeded())
        {
            return AssetResult<MaterialDesc>(valid);
        }
        if (!program || program->index().shader_name != data.shader_name ||
            program->programs().front()->data().contract.usage != shader::ShaderUsage::Material ||
            !program->find(shader::ShaderPassRole::Forward, program->programs().front()->data().contract.vertex_factory)
                 .succeeded())
        {
            return AssetResult<MaterialDesc>(failure("A matching compiled Forward Shader program is required."));
        }
        const auto configuration = shader::resolve_shader_permutation(program->index().material_domain,
                                                                      material_static_selections(data.static_options));
        if (!configuration.succeeded())
        {
            return AssetResult<MaterialDesc>(failure(configuration.errors.front().message));
        }
        if (configuration.permutation->key != program->index().permutation_key)
        {
            return AssetResult<MaterialDesc>(failure("Compiled ShaderMap does not match the material static options."));
        }
        MaterialDesc desc;
        desc.shader_name = data.shader_name;
        desc.parameter_schema =
            material_parameter_schema_from_shader_schema(program->programs().front()->data().parameter_schema);
        desc.shader_map = std::move(program);
        desc.static_options = data.static_options;
        desc.two_sided = data.two_sided;
        std::string error;
        if (!initialize_material_constant_defaults(desc, error))
        {
            return AssetResult<MaterialDesc>(failure(error));
        }
        for (const auto& resource : desc.parameter_schema.resources)
        {
            if (resource.category == shader::ShaderParameterCategory::Sampler &&
                resource.resource_kind == shader::ResourceKind::Sampler && resource.array_count == 1u &&
                resource.default_value_kind == shader::ShaderParameterDefaultValueKind::Identifier)
            {
                MaterialSamplerPreset preset{};
                if (!parse_material_sampler_preset(resource.default_value, preset))
                {
                    return AssetResult<MaterialDesc>(
                        failure("Unknown material sampler default: " + resource.default_value));
                }
                desc.sampler_defaults.emplace(resource.parameter_id, preset);
                continue;
            }
            if (resource.category != shader::ShaderParameterCategory::SampledTexture ||
                resource.resource_kind != shader::ResourceKind::Texture2D || resource.array_count != 1u ||
                resource.default_value_kind != shader::ShaderParameterDefaultValueKind::String)
            {
                return AssetResult<MaterialDesc>(failure("Unsupported material resource: " + resource.name));
            }
            const auto found = textures.named_defaults.find(resource.default_value);
            if (found == textures.named_defaults.end() || !found->second)
            {
                if (!material_parameter_is_active(*desc.shader_map, resource.parameter_id))
                {
                    desc.texture_defaults.emplace(resource.parameter_id, nullptr);
                    continue;
                }
                return AssetResult<MaterialDesc>(
                    failure("Unresolved builtin texture default: " + resource.default_value));
            }
            desc.texture_defaults.emplace(resource.parameter_id, found->second);
        }
        return AssetResult<MaterialDesc>(std::move(desc));
    }

    AssetResult<MaterialParameterChanges> material_changes_from_overrides(
        const std::vector<MaterialParameterOverride>& overrides, const shader::ShaderParameterSchema& schema,
        const MaterialTextureValues& textures)
    {
        const auto valid = check_runtime_values(overrides, schema, textures);
        if (!valid.succeeded())
        {
            return AssetResult<MaterialParameterChanges>(valid);
        }
        MaterialParameterChanges changes;
        for (const auto& item : overrides)
        {
            if (!material_override_matches_schema(item, schema))
            {
                continue;
            }
            MaterialParameterChange change;
            change.name = item.name;
            // C++17 get_if translates persisted DTO values at the GT boundary.
            if (const auto* value = std::get_if<float>(&item.value))
            {
                change.value = *value;
            }
            else if (const auto* value = std::get_if<Vector2>(&item.value))
            {
                change.value = *value;
            }
            else if (const auto* value = std::get_if<Vector3>(&item.value))
            {
                change.value = *value;
            }
            else if (const auto* value = std::get_if<Vector4>(&item.value))
            {
                change.value = *value;
            }
            else if (const auto* value = std::get_if<AssetRef>(&item.value))
            {
                change.value = textures.assets.at(value->asset_id);
            }
            else if (const auto* value = std::get_if<MaterialSamplerPreset>(&item.value))
            {
                change.value = *value;
            }
            else
            {
                return AssetResult<MaterialParameterChanges>(failure("Unsupported material value: " + item.name));
            }
            changes.push_back(std::move(change));
        }
        return AssetResult<MaterialParameterChanges>(std::move(changes));
    }

    AssetResult<MaterialInstanceRef> create_material_from_asset(const MaterialAssetData& data,
                                                                ShaderMapCollectionRef program,
                                                                const MaterialTextureValues& textures)
    {
        auto descriptor = material_descriptor_from_asset(data, std::move(program), textures);
        if (!descriptor.succeeded())
        {
            return AssetResult<MaterialInstanceRef>(descriptor.status());
        }
        MaterialRef material = Material::create(descriptor.value());
        if (!material)
        {
            return AssetResult<MaterialInstanceRef>(failure("Material schema or defaults are invalid."));
        }
        return build_instance(std::move(material), data.overrides, {}, textures);
    }

    AssetResult<MaterialInstanceRef> create_material_instance_from_asset(const MaterialInstanceAssetData& data,
                                                                         MaterialInterfaceRef parent,
                                                                         const MaterialTextureValues& textures,
                                                                         ShaderMapCollectionRef configuration)
    {
        const auto valid = validate_material_instance_asset(data);
        if (!valid.succeeded())
        {
            return AssetResult<MaterialInstanceRef>(valid);
        }
        if (!parent)
        {
            return AssetResult<MaterialInstanceRef>(failure("Instance Parent is unavailable."));
        }
        const auto changes = material_changes_from_overrides(data.overrides, parent->parameter_schema(), textures);
        if (!changes.succeeded())
        {
            return AssetResult<MaterialInstanceRef>(changes.status());
        }
        if (!data.static_options.empty())
        {
            if (!parent->desc().shader_map)
            {
                return AssetResult<MaterialInstanceRef>(
                    failure("Static options require a validated Parent ShaderMap."));
            }
            std::map<std::string, shader::ShaderPermutationSelection> selected;
            for (const auto& value : material_static_selections(parent->effective_static_options()))
            {
                selected[value.name] = value;
            }
            for (const auto& value : material_static_selections(data.static_options))
            {
                selected[value.name] = value;
            }
            std::vector<shader::ShaderPermutationSelection> selections;
            for (const auto& value : selected)
            {
                selections.push_back(value.second);
            }
            const auto resolved =
                shader::resolve_shader_permutation(parent->desc().shader_map->index().material_domain, selections);
            if (!resolved.succeeded())
            {
                return AssetResult<MaterialInstanceRef>(failure(resolved.errors.front().message));
            }
            if (!configuration)
            {
                configuration = parent->desc().shader_map;
            }
            if (configuration->index().permutation_key != resolved.permutation->key)
            {
                return AssetResult<MaterialInstanceRef>(
                    failure("The instance static configuration must be compiled before creation."));
            }
        }
        auto child = MaterialInstance::create(std::move(parent), std::move(configuration), data.static_options);
        if (!child)
        {
            return AssetResult<MaterialInstanceRef>(failure("Could not create MaterialInstance."));
        }
        try
        {
            if (!child->apply_parameters(changes.value()))
            {
                MaterialInstance::release(child);
                return AssetResult<MaterialInstanceRef>(failure("Could not apply instance overrides."));
            }
        }
        catch (const std::exception& error)
        {
            MaterialInstance::release(child);
            return AssetResult<MaterialInstanceRef>(failure(error.what()));
        }
        return AssetResult<MaterialInstanceRef>(std::move(child));
    }
} // namespace toy3d
