#include "rendercore/material/material_asset_builder.h"

#include <exception>

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
            const shader::ShaderParameterSchema& schema, const MaterialTextureValues& textures)
        {
            for (const auto& item : values)
            {
                // Known persisted orphans survive saves; only matching values reach runtime setters.
                if (!material_override_matches_schema(item, schema)) continue;
                // C++17 get_if resolves Texture references without erasing their Asset identity.
                if (const auto* reference = std::get_if<AssetRef>(&item.value))
                {
                    const auto found = textures.assets.find(reference->asset_id);
                    if (found == textures.assets.end() || !found->second)
                        return failure("Texture asset is not loaded for parameter: " + item.name);
                }
                // Sampler DTOs are persisted now; actual sampling is introduced in M5.
                if (std::holds_alternative<MaterialSamplerPreset>(item.value))
                    return failure("Runtime Sampler parameters are not supported yet: " + item.name);
            }
            return AssetStatus::success();
        }

        AssetResult<MaterialInstanceRef> build_instance(MaterialRef material,
            const std::vector<MaterialParameterOverride>& root,
            const std::vector<MaterialParameterOverride>& child, const MaterialTextureValues& textures)
        {
            const auto& schema = material->parameter_schema();
            AssetStatus valid = check_runtime_values(root, schema, textures);
            if (valid.succeeded()) valid = check_runtime_values(child, schema, textures);
            if (!valid.succeeded()) return AssetResult<MaterialInstanceRef>(valid);
            std::map<std::string, const MaterialParameterOverride*> effective;
            for (const auto& item : root) if (material_override_matches_schema(item, schema)) effective[item.name] = &item;
            for (const auto& item : child) if (material_override_matches_schema(item, schema)) effective[item.name] = &item;
            MaterialInstanceRef candidate = MaterialInstance::create(std::move(material));
            if (!candidate) return AssetResult<MaterialInstanceRef>(failure("Could not create runtime material instance."));
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
                    if (const auto* value = std::get_if<float>(&item.value)) change.value = *value;
                    else if (const auto* value = std::get_if<Vector2>(&item.value)) change.value = *value;
                    else if (const auto* value = std::get_if<Vector3>(&item.value)) change.value = *value;
                    else if (const auto* value = std::get_if<Vector4>(&item.value)) change.value = *value;
                    else if (const auto* value = std::get_if<AssetRef>(&item.value)) change.value = textures.assets.at(value->asset_id);
                    else return AssetResult<MaterialInstanceRef>(failure("Unsupported material parameter: " + item.name));
                    changes.push_back(std::move(change));
                }
                if (!candidate->apply_parameters(changes))
                {
                    MaterialInstance::release(candidate);
                    return AssetResult<MaterialInstanceRef>(failure("Could not apply the complete material parameter batch."));
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
    }

    AssetResult<MaterialInstanceRef> create_material_from_asset(const MaterialAssetData& data,
        std::shared_ptr<const ShaderMapProgram> program, const MaterialTextureValues& textures)
    {
        const AssetStatus valid = validate_material_asset(data);
        if (!valid.succeeded()) return AssetResult<MaterialInstanceRef>(valid);
        if (!program || program->data().shader_name != data.shader_name || program->data().pass_name != "Forward")
            return AssetResult<MaterialInstanceRef>(failure("A matching compiled Forward Shader program is required."));
        MaterialDesc desc;
        desc.shader_name = data.shader_name;
        desc.parameter_schema = material_parameter_schema_from_shader_schema(program->data().parameter_schema);
        desc.shader_program = std::move(program);
        desc.two_sided = data.two_sided;
        std::string error;
        if (!initialize_material_constant_defaults(desc, error)) return AssetResult<MaterialInstanceRef>(failure(error));
        for (const auto& resource : desc.parameter_schema.resources)
        {
            if (resource.category != shader::ShaderParameterCategory::SampledTexture ||
                resource.resource_kind != shader::ResourceKind::Texture2D || resource.array_count != 1u ||
                resource.default_value_kind != shader::ShaderParameterDefaultValueKind::String)
                return AssetResult<MaterialInstanceRef>(failure("Unsupported material resource: " + resource.name));
            const auto found = textures.named_defaults.find(resource.default_value);
            if (found == textures.named_defaults.end() || !found->second)
                return AssetResult<MaterialInstanceRef>(failure("Unresolved builtin texture default: " + resource.default_value));
            desc.texture_defaults.emplace(resource.parameter_id, found->second);
        }
        MaterialRef material = Material::create(std::move(desc));
        if (!material) return AssetResult<MaterialInstanceRef>(failure("Material schema or defaults are invalid."));
        return build_instance(std::move(material), data.overrides, {}, textures);
    }

    AssetResult<MaterialInstanceRef> create_material_instance_from_asset(const MaterialInstanceAssetData& data,
        const MaterialAssetData& parent_data, MaterialRef shader_material, const MaterialTextureValues& textures)
    {
        AssetStatus valid = validate_material_instance_asset(data);
        if (valid.succeeded()) valid = validate_material_asset(parent_data);
        if (!valid.succeeded()) return AssetResult<MaterialInstanceRef>(valid);
        if (!shader_material || shader_material->desc().shader_name != parent_data.shader_name ||
            shader_material->desc().two_sided != parent_data.two_sided)
            return AssetResult<MaterialInstanceRef>(failure("Instance parent does not match its immutable Shader material."));
        return build_instance(std::move(shader_material), parent_data.overrides, data.overrides, textures);
    }
}
