#include "rendercore/material/material.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <functional>
#include <map>
#include <set>
#include <utility>

#include "logging/logger.h"
#include "asset/material/material_asset.h"
#include "rendercore/render_command.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/material/material_render_proxy.h"

namespace toy3d
{
    bool material_parameter_is_active(const ShaderMapCollection& shader_map, ShaderParameterId parameter_id)
    {
        for (const auto& program : shader_map.programs())
        {
            if (std::any_of(program->data().bindings.begin(), program->data().bindings.end(),
                            [parameter_id](const auto& binding)
                            {
                                return binding.group == RHIBindingGroup::Material &&
                                       binding.parameter_id == parameter_id;
                            }))
            {
                return true;
            }
        }
        return false;
    }

    bool validate_material_mesh_pass(const MaterialDesc& desc, shader::ShaderPassRole role,
                                     shader::VertexFactoryType factory, std::string& error)
    {
        if (!desc.shader_map)
        {
            return true;
        }
        const auto selected = desc.shader_map->find(role, factory);
        if (selected.succeeded())
        {
            return true;
        }
        const auto& name = desc.shader_map->index().shader_name;
        if ((role == shader::ShaderPassRole::ShadowDepth || role == shader::ShaderPassRole::HitProxy) &&
            desc.shader_map->programs().front()->data().contract.surface_mode == shader::ShaderSurfaceMode::Opaque)
        {
            return true;
        }
        error = "Custom material " + name + " is missing the required mesh Pass role: " + selected.error;
        return false;
    }

    bool validate_material_geometry(const MaterialDesc& desc, shader::VertexFactoryType factory, bool has_vertex_colors,
                                    bool has_valid_tangent_frame, std::string& error)
    {
        if (!desc.shader_map)
        {
            return true;
        }
        if (desc.shader_map->requires_tangent_frame() && !has_valid_tangent_frame)
        {
            error = desc.shader_name + " requires a valid tangent frame for this static configuration.";
            return false;
        }
        const auto forward = desc.shader_map->find(shader::ShaderPassRole::Forward, factory);
        if (!forward.succeeded())
        {
            error = forward.error;
            return false;
        }
        for (const auto& program : desc.shader_map->programs())
        {
            if (program->data().contract.vertex_factory != factory)
            {
                continue;
            }
            for (const auto& input : program->data().vertex_inputs)
            {
                if (factory == shader::VertexFactoryType::Local &&
                    (input.attribute_id == ShaderVertexAttributeId::BlendIndices0 ||
                     input.attribute_id == ShaderVertexAttributeId::BlendIndices1 ||
                     input.attribute_id == ShaderVertexAttributeId::BlendWeights0 ||
                     input.attribute_id == ShaderVertexAttributeId::BlendWeights1))
                {
                    error = desc.shader_name + "/" + program->data().pass_name +
                            " requires skin attributes on a Local mesh.";
                    return false;
                }
                if (input.attribute_id == ShaderVertexAttributeId::Color0 && !has_vertex_colors)
                {
                    error = desc.shader_name + "/" + program->data().pass_name +
                            " requires COLOR0, but the mesh has no vertex colors.";
                    return false;
                }
            }
        }
        return true;
    }

    shader::ShaderParameterSchema material_parameter_schema_from_shader_schema(
        const shader::ShaderParameterSchema& source)
    {
        shader::ShaderParameterSchema result;
        result.generated_format_version = source.generated_format_version;
        result.shader_abi_version = source.shader_abi_version;
        result.parameter_id_version = source.parameter_id_version;
        result.editor_properties_hash = source.editor_properties_hash;
        for (const shader::ShaderParameterConstantBufferSchema& buffer : source.constant_buffers)
        {
            if (buffer.group == shader::BindingGroup::Material)
            {
                result.constant_buffers.push_back(buffer);
            }
        }
        for (const shader::ShaderParameterResourceSchema& resource : source.resources)
        {
            if (resource.group == shader::BindingGroup::Material)
            {
                result.resources.push_back(resource);
            }
        }
        result.logical_layout_hash = shader::calculate_shader_parameter_logical_layout_hash(result);
        result.schema_identity = shader::calculate_shader_parameter_schema_identity(result);
        return result;
    }

    namespace
    {
        struct ResolvedMaterialParameter
        {
            ShaderParameterId id = 0;
            MaterialParameterValue value;
        };

        void release_material_textures(MaterialDesc& desc, MaterialParameterChanges& changes)
        {
            for (auto& value : desc.texture_defaults)
            {
                value.second.reset();
            }
            for (auto& value : changes)
            {
                // C++17 get_if retires only the resource branch of old values.
                auto* texture = std::get_if<TextureRef>(&value.value);
                if (!texture)
                {
                    continue;
                }
                texture->reset();
            }
        }

        bool resolve_material_changes(const MaterialDesc& desc, const MaterialParameterChanges& changes,
                                      std::vector<ResolvedMaterialParameter>& resolved)
        {
            std::set<std::string> names;
            for (const auto& change : changes)
            {
                if (!names.insert(change.name).second)
                {
                    return false;
                }
                ResolvedMaterialParameter item;
                item.value = change.value;
                // C++17 get_if keeps this closed set readable and permits typed
                // reset defaults without passing variant machinery to the RT proxy.
                const bool reset = std::holds_alternative<std::monostate>(change.value);
                for (const auto& buffer : desc.parameter_schema.constant_buffers)
                {
                    for (const auto& member : buffer.members)
                    {
                        if (member.name == change.name)
                        {
                            item.id = member.parameter_id;
                            if (member.type == shader::ShaderValueType::Float32)
                            {
                                if (reset)
                                {
                                    item.value = desc.scalar_defaults.at(item.id);
                                }
                                const auto* value = std::get_if<float>(&item.value);
                                if (!value || !shader::validate_shader_scalar_value(member, *value))
                                {
                                    return false;
                                }
                            }
                            else if (member.type == shader::ShaderValueType::Float32x2)
                            {
                                if (reset)
                                {
                                    const auto& value = desc.vector2_defaults.at(item.id);
                                    item.value = Vector2(value.x, value.y);
                                }
                                const auto* value = std::get_if<Vector2>(&item.value);
                                if (!value || !std::isfinite(value->x) || !std::isfinite(value->y))
                                {
                                    return false;
                                }
                            }
                            else if (member.type == shader::ShaderValueType::Float32x3)
                            {
                                if (reset)
                                {
                                    const auto& value = desc.vector3_defaults.at(item.id);
                                    item.value = Vector3(value.x, value.y, value.z);
                                }
                                const auto* value = std::get_if<Vector3>(&item.value);
                                if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) ||
                                    !std::isfinite(value->z))
                                {
                                    return false;
                                }
                            }
                            else if (member.type == shader::ShaderValueType::Float32x4)
                            {
                                if (reset)
                                {
                                    const auto& value = desc.vector4_defaults.at(item.id);
                                    item.value = Vector4(value.x, value.y, value.z, value.w);
                                }
                                const auto* value = std::get_if<Vector4>(&item.value);
                                if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) ||
                                    !std::isfinite(value->z) || !std::isfinite(value->w))
                                {
                                    return false;
                                }
                            }
                            else
                            {
                                return false;
                            }
                        }
                    }
                }
                for (const auto& resource : desc.parameter_schema.resources)
                {
                    if (resource.name == change.name)
                    {
                        item.id = resource.parameter_id;
                        if (resource.array_count != 1u)
                        {
                            return false;
                        }
                        if (resource.category == shader::ShaderParameterCategory::SampledTexture &&
                            resource.resource_kind == shader::ResourceKind::Texture2D)
                        {
                            if (reset)
                            {
                                item.value = desc.texture_defaults.at(item.id);
                            }
                            const auto* value = std::get_if<TextureRef>(&item.value);
                            if (!value ||
                                (!*value && (!reset || !desc.shader_map ||
                                             material_parameter_is_active(*desc.shader_map, item.id))) ||
                                (*value && ((*value)->desc().cube || (*value)->desc().usage != resource.texture_usage)))
                            {
                                return false;
                            }
                        }
                        else if (resource.category == shader::ShaderParameterCategory::Sampler &&
                                 resource.resource_kind == shader::ResourceKind::Sampler)
                        {
                            if (reset)
                            {
                                item.value = desc.sampler_defaults.at(item.id);
                            }
                            const auto* value = std::get_if<MaterialSamplerPreset>(&item.value);
                            if (!value || *value < MaterialSamplerPreset::PointClamp ||
                                *value > MaterialSamplerPreset::TrilinearWrap)
                            {
                                return false;
                            }
                        }
                        else
                        {
                            return false;
                        }
                    }
                }
                if (!item.id)
                {
                    return false;
                }
                resolved.push_back(std::move(item));
            }
            return true;
        }

        bool validate_material_schema_defaults(const MaterialDesc& desc, std::string& error)
        {
            std::size_t scalar_count = 0u;
            std::size_t vector2_count = 0u;
            std::size_t vector3_count = 0u;
            std::size_t vector4_count = 0u;
            for (const shader::ShaderParameterConstantBufferSchema& buffer : desc.parameter_schema.constant_buffers)
            {
                if (buffer.group != shader::BindingGroup::Material)
                {
                    error = "Material parameter schema contains a non-Material constant buffer";
                    return false;
                }
                for (const shader::ShaderParameterConstantMemberSchema& member : buffer.members)
                {
                    if (member.default_value.size() != member.size)
                    {
                        error = "Material constant schema is missing its canonical default value";
                        return false;
                    }
                    bool has_runtime_default = false;
                    switch (member.type)
                    {
                    case shader::ShaderValueType::Float32:
                        ++scalar_count;
                        has_runtime_default =
                            desc.scalar_defaults.count(member.parameter_id) == 1u &&
                            shader::validate_shader_scalar_value(member, desc.scalar_defaults.at(member.parameter_id));
                        break;
                    case shader::ShaderValueType::Float32x2:
                        ++vector2_count;
                        has_runtime_default = desc.vector2_defaults.count(member.parameter_id) == 1u;
                        break;
                    case shader::ShaderValueType::Float32x3:
                        ++vector3_count;
                        has_runtime_default = desc.vector3_defaults.count(member.parameter_id) == 1u;
                        break;
                    case shader::ShaderValueType::Float32x4:
                        ++vector4_count;
                        has_runtime_default = desc.vector4_defaults.count(member.parameter_id) == 1u;
                        break;
                    default:
                        error = "Material schema contains an unsupported first-stage constant type";
                        return false;
                    }
                    if (!has_runtime_default)
                    {
                        error = "Material is missing a runtime value for a schema constant default";
                        return false;
                    }
                }
            }
            if (scalar_count != desc.scalar_defaults.size() || vector2_count != desc.vector2_defaults.size() ||
                vector3_count != desc.vector3_defaults.size() || vector4_count != desc.vector4_defaults.size())
            {
                error = "Material contains a constant default that is not declared by its complete schema";
                return false;
            }
            std::size_t texture_count = 0u;
            std::size_t sampler_count = 0u;
            for (const shader::ShaderParameterResourceSchema& resource : desc.parameter_schema.resources)
            {
                if (resource.group != shader::BindingGroup::Material)
                {
                    error = "Material parameter schema contains a non-Material resource";
                    return false;
                }
                if (resource.category == shader::ShaderParameterCategory::Sampler &&
                    resource.resource_kind == shader::ResourceKind::Sampler && resource.array_count == 1u)
                {
                    if (resource.default_value_kind != shader::ShaderParameterDefaultValueKind::Identifier ||
                        desc.sampler_defaults.count(resource.parameter_id) != 1u ||
                        desc.sampler_defaults.at(resource.parameter_id) < MaterialSamplerPreset::PointClamp ||
                        desc.sampler_defaults.at(resource.parameter_id) > MaterialSamplerPreset::TrilinearWrap)
                    {
                        error = "Material Sampler schema is missing a supported runtime default";
                        return false;
                    }
                    ++sampler_count;
                    continue;
                }
                if (resource.category != shader::ShaderParameterCategory::SampledTexture ||
                    resource.resource_kind != shader::ResourceKind::Texture2D || resource.array_count != 1u)
                {
                    error = "Material schema contains an unsupported first-stage resource type";
                    return false;
                }
                if (resource.default_value_kind == shader::ShaderParameterDefaultValueKind::None ||
                    desc.texture_defaults.count(resource.parameter_id) != 1u ||
                    (!desc.texture_defaults.at(resource.parameter_id) &&
                     (!desc.shader_map || material_parameter_is_active(*desc.shader_map, resource.parameter_id))))
                {
                    error = "Material Texture schema is missing its canonical or runtime default";
                    return false;
                }
                const auto& texture = desc.texture_defaults.at(resource.parameter_id);
                if (texture && (texture->desc().cube || texture->desc().usage != resource.texture_usage))
                {
                    error = "Material Texture usage differs from Shader requirement: " + resource.name;
                    return false;
                }
                ++texture_count;
            }
            if (texture_count != desc.texture_defaults.size() || sampler_count != desc.sampler_defaults.size())
            {
                error = "Material contains a Texture default that is not declared by its complete schema";
                return false;
            }
            return true;
        }

        bool is_shader_map_compatible_with_material(const MaterialDesc& desc, const ShaderMapCollection& program,
                                                    std::string& error)
        {
            error.clear();
            if (program.index().shader_name != desc.shader_name ||
                program.programs().front()->data().contract.usage != shader::ShaderUsage::Material)
            {
                error = "ShaderMap Program identity does not match the Material shader";
                return false;
            }
            const shader::ShaderParameterSchema program_material_schema =
                material_parameter_schema_from_shader_schema(program.programs().front()->data().parameter_schema);
            if (program_material_schema.schema_identity != desc.parameter_schema.schema_identity)
            {
                error = "ShaderMap Program complete Material schema does not match the Material schema identity";
                return false;
            }
            return true;
        }

    } // namespace

    bool initialize_material_constant_defaults(MaterialDesc& desc, std::string& error)
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t), "Material defaults require binary32");
        if (!shader::validate_shader_parameter_schema(desc.parameter_schema, error))
        {
            return false;
        }
        std::unordered_map<ShaderParameterId, float> scalars;
        std::unordered_map<ShaderParameterId, vec2> vectors2;
        std::unordered_map<ShaderParameterId, vec3> vectors3;
        std::unordered_map<ShaderParameterId, vec4> vectors4;
        for (const auto& buffer : desc.parameter_schema.constant_buffers)
        {
            if (buffer.group != shader::BindingGroup::Material)
            {
                error = "Material defaults require a Material-only schema.";
                return false;
            }
            for (const auto& member : buffer.members)
            {
                std::uint32_t count = 0;
                switch (member.type)
                {
                case shader::ShaderValueType::Float32:
                    count = 1u;
                    break;
                case shader::ShaderValueType::Float32x2:
                    count = 2u;
                    break;
                case shader::ShaderValueType::Float32x3:
                    count = 3u;
                    break;
                case shader::ShaderValueType::Float32x4:
                    count = 4u;
                    break;
                default:
                    break;
                }
                if (count == 0u || member.array_count != 1u || member.array_stride != 0u ||
                    member.matrix_stride != 0u || member.size != count * sizeof(float) ||
                    member.default_value.size() != member.size)
                {
                    error = "Material default is missing or uses an unsupported constant shape: " + member.name;
                    return false;
                }
                float values[4] = {};
                for (std::uint32_t component = 0; component < count; ++component)
                {
                    std::uint32_t bits = 0;
                    for (std::uint32_t byte = 0; byte < sizeof(bits); ++byte)
                    {
                        bits |= static_cast<std::uint32_t>(member.default_value[component * sizeof(bits) + byte])
                                << (byte * 8u);
                    }
                    std::memcpy(&values[component], &bits, sizeof(bits));
                    if (!std::isfinite(values[component]) ||
                        (count == 1u && !shader::validate_shader_scalar_value(member, values[component])))
                    {
                        error = "Material default is not finite: " + member.name;
                        return false;
                    }
                }
                switch (count)
                {
                case 1u:
                    scalars.emplace(member.parameter_id, values[0]);
                    break;
                case 2u:
                    vectors2.emplace(member.parameter_id, vec2(values[0], values[1]));
                    break;
                case 3u:
                    vectors3.emplace(member.parameter_id, vec3(values[0], values[1], values[2]));
                    break;
                case 4u:
                    vectors4.emplace(member.parameter_id, vec4(values[0], values[1], values[2], values[3]));
                    break;
                }
            }
        }
        desc.scalar_defaults = std::move(scalars);
        desc.vector2_defaults = std::move(vectors2);
        desc.vector3_defaults = std::move(vectors3);
        desc.vector4_defaults = std::move(vectors4);
        error.clear();
        return true;
    }

    // --------------------------------------------------------------------------
    // Material: Root Shader, defaults and Library-controlled configuration
    // --------------------------------------------------------------------------
    std::shared_ptr<Material> Material::create(MaterialDesc desc)
    {
        if (desc.shader_name.empty())
        {
            TOY_LOG_ERROR("A Material must identify a ShaderMap shader.");
            return nullptr;
        }
        std::string schema_error;
        if (desc.parameter_schema.schema_identity == Sha256Hash{} &&
            desc.parameter_schema.logical_layout_hash == Sha256Hash{} &&
            desc.parameter_schema.constant_buffers.empty() && desc.parameter_schema.resources.empty())
        {
            desc.parameter_schema.logical_layout_hash =
                shader::calculate_shader_parameter_logical_layout_hash(desc.parameter_schema);
            desc.parameter_schema.schema_identity =
                shader::calculate_shader_parameter_schema_identity(desc.parameter_schema);
        }
        if (!shader::validate_shader_parameter_schema(desc.parameter_schema, schema_error) ||
            !validate_material_schema_defaults(desc, schema_error))
        {
            TOY_LOG_ERROR("Invalid Material parameter schema: {}.", schema_error);
            return nullptr;
        }
        if (desc.shader_map)
        {
            std::string error;
            if (!is_shader_map_compatible_with_material(desc, *desc.shader_map, error))
            {
                TOY_LOG_ERROR("Invalid Material ShaderMap Program: {}.", error);
                return nullptr;
            }
        }
        Material material(std::move(desc));
        return std::make_shared<Material>(std::move(material));
    }

    Material::Material(MaterialDesc desc) : MaterialInterface(std::move(desc))
    {
    }

    // --------------------------------------------------------------------------
    // MaterialInterface: GT configuration and stable FIFO-protected RT identity
    // --------------------------------------------------------------------------
    MaterialInterface::MaterialInterface(MaterialDesc desc)
        : desc_(std::move(desc)), shader_map_(desc_.shader_map), two_sided_(desc_.two_sided),
          material_render_proxy_(std::make_unique<MaterialRenderProxy>(desc_))
    {
    }

    MaterialInterface::MaterialInterface(MaterialInterface&& other) noexcept
        : desc_(std::move(other.desc_)), local_overrides_(std::move(other.local_overrides_)),
          children_(std::move(other.children_)), shader_map_(std::move(other.shader_map_)),
          pending_shader_map_(std::move(other.pending_shader_map_)), two_sided_(other.two_sided_),
          pending_two_sided_(other.pending_two_sided_),
          replacement_commit_complete_(std::move(other.replacement_commit_complete_)),
          replacement_commit_succeeded_(std::move(other.replacement_commit_succeeded_)),
          replacement_publication_pending_(other.replacement_publication_pending_),
          material_render_proxy_(std::move(other.material_render_proxy_)), render_proxy_used_(other.render_proxy_used_),
          release_enqueued_(other.release_enqueued_)
    {
        other.render_proxy_used_ = false;
        other.release_enqueued_ = false;
        other.replacement_publication_pending_ = false;
    }

    MaterialInterface::~MaterialInterface()
    {
        if (render_proxy_used_ && !release_enqueued_ && material_render_proxy_)
        {
            TOY_LOG_ERROR("A used MaterialRenderProxy requires explicit FIFO release.");
            std::terminate();
        }
    }

    void MaterialInterface::retire_proxy()
    {
        if (!material_render_proxy_)
        {
            return;
        }
        if (!render_proxy_used_)
        {
            material_render_proxy_.reset();
            return;
        }
        // Shared ownership of the unique allocation allows failed admission to
        // restore the exact address borrowed by previously accepted commands.
        auto values = std::make_shared<std::pair<MaterialDesc, MaterialParameterChanges>>(desc_, local_overrides_);
        auto owner = std::make_shared<std::unique_ptr<MaterialRenderProxy>>(std::move(material_render_proxy_));
        try
        {
            enqueue_render_command("ReleaseMaterialRenderProxy",
                                   [owner, values]() noexcept
                                   {
                                       owner->reset();
                                       release_material_textures(values->first, values->second);
                                   });
        }
        catch (...)
        {
            material_render_proxy_ = std::move(*owner);
            throw;
        }
        release_enqueued_ = true;
    }

    void MaterialInterface::release(MaterialInterfaceRef& material)
    {
        if (!material)
        {
            return;
        }
        if (material.use_count() != 1)
        {
            throw std::invalid_argument("Material final release requires its last reference");
        }
        // The last GT owner keeps configuration/TextureRefs alive through final
        // proxy destruction. Const callers cannot mutate parameter state.
        if (material->render_proxy_used_)
        {
            material->release_enqueued_ = true;
            try
            {
                enqueue_render_command("ReleaseMaterialInterface",
                                       [owner = material]() noexcept
                                       {
                                       });
            }
            catch (...)
            {
                material->release_enqueued_ = false;
                throw;
            }
        }
        material.reset();
    }

    // --------------------------------------------------------------------------
    // MaterialInstance: Direct Parent ownership and local mutable overrides
    // --------------------------------------------------------------------------
    MaterialInstanceRef MaterialInstance::create(MaterialInterfaceRef parent, ShaderMapCollectionRef configuration)
    {
        auto options = configuration ? material_static_options(configuration->index().material_selections)
                                     : std::vector<MaterialStaticOption>{};
        return create(std::move(parent), std::move(configuration), std::move(options));
    }

    MaterialInstanceRef MaterialInstance::create(MaterialInterfaceRef parent, ShaderMapCollectionRef configuration,
                                                 std::vector<MaterialStaticOption> static_options)
    {
        if (!parent)
        {
            TOY_LOG_ERROR("A MaterialInstance requires a Parent.");
            return {};
        }
        MaterialRef root = std::dynamic_pointer_cast<const Material>(parent);
        if (!root)
        {
            const auto instance = std::dynamic_pointer_cast<const MaterialInstance>(parent);
            if (!instance)
            {
                return {};
            }
            root = instance->material();
        }
        std::size_t depth = 1u;
        for (auto ancestor = parent; ancestor; ancestor = ancestor->parent())
        {
            if (++depth > maximum_material_parent_depth)
            {
                TOY_LOG_ERROR("Material Parent depth exceeds {}.", maximum_material_parent_depth);
                return {};
            }
        }
        MaterialInstance value(parent, std::move(root));
        value.desc_.static_options = std::move(static_options);
        const auto selected = configuration ? configuration : parent->desc().shader_map;
        if (selected)
        {
            const auto resolved = shader::resolve_shader_permutation(
                selected->index().material_domain,
                material_static_selections(
                    merge_material_static_options(parent->effective_static_options(), value.desc_.static_options)));
            if (!resolved.succeeded() || resolved.permutation->key != selected->index().permutation_key)
            {
                TOY_LOG_ERROR("MaterialInstance local static options do not match its compiled configuration.");
                return {};
            }
        }
        if (configuration)
        {
            const auto schema = material_parameter_schema_from_shader_schema(
                configuration->programs().front()->data().parameter_schema);
            if (configuration->index().shader_name != parent->desc().shader_name ||
                schema.schema_identity != parent->parameter_schema().schema_identity)
            {
                TOY_LOG_ERROR("MaterialInstance static configuration has an incompatible Shader/schema.");
                return {};
            }
            value.desc_.shader_map = configuration;
            value.shader_map_ = std::move(configuration);
        }
        auto result = std::make_shared<MaterialInstance>(std::move(value));
        parent->children_.push_back(result);
        // No inherited value becomes a local override.
        MaterialDesc effective = result->desc();
        for (const auto& buffer : effective.parameter_schema.constant_buffers)
        {
            for (const auto& member : buffer.members)
            {
                MaterialParameterValue parameter;
                if (!parent->parameter_value(member.name, parameter))
                {
                    return {};
                }
                // C++17 get_if transfers inherited values into the initial RT
                // candidate, while the GT local override collection stays empty.
                if (const auto* number = std::get_if<float>(&parameter))
                {
                    effective.scalar_defaults[member.parameter_id] = *number;
                }
                else if (const auto* vector = std::get_if<Vector2>(&parameter))
                {
                    effective.vector2_defaults[member.parameter_id] = vec2(vector->x, vector->y);
                }
                else if (const auto* vector = std::get_if<Vector3>(&parameter))
                {
                    effective.vector3_defaults[member.parameter_id] = vec3(vector->x, vector->y, vector->z);
                }
                else if (const auto* vector = std::get_if<Vector4>(&parameter))
                {
                    effective.vector4_defaults[member.parameter_id] = vec4(vector->x, vector->y, vector->z, vector->w);
                }
            }
        }
        for (const auto& resource : effective.parameter_schema.resources)
        {
            MaterialParameterValue parameter;
            if (!parent->parameter_value(resource.name, parameter))
            {
                return {};
            }
            if (const auto* texture = std::get_if<TextureRef>(&parameter))
            {
                effective.texture_defaults[resource.parameter_id] = *texture;
            }
            else if (const auto* sampler = std::get_if<MaterialSamplerPreset>(&parameter))
            {
                effective.sampler_defaults[resource.parameter_id] = *sampler;
            }
            else
            {
                return {};
            }
        }
        result->material_render_proxy_ = std::make_unique<MaterialRenderProxy>(effective);
        return result;
    }

    MaterialInstance::MaterialInstance(MaterialInterfaceRef parent, MaterialRef root)
        : MaterialInterface(parent->desc()), parent_(std::move(parent)), material_(std::move(root))
    {
    }

    MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
        : MaterialInterface(std::move(other)), parent_(std::move(other.parent_)), material_(std::move(other.material_))
    {
    }

    void MaterialInstance::release(MaterialInstanceRef& instance)
    {
        if (!instance)
        {
            return;
        }
        if (instance.use_count() != 1)
        {
            throw std::invalid_argument("MaterialInstance final release requires its last reference");
        }
        if (instance->render_proxy_used_)
        {
            instance->release_enqueued_ = true;
            try
            {
                enqueue_render_command("ReleaseMaterialInstance",
                                       [owner = instance]() noexcept
                                       {
                                       });
            }
            catch (...)
            {
                instance->release_enqueued_ = false;
                throw;
            }
        }
        instance.reset();
    }

    bool MaterialInstance::set_scalar(std::string_view name, float value)
    {
        return apply_parameters({{std::string(name), value}});
    }

    bool MaterialInstance::set_vector(std::string_view name, const vec2& value)
    {
        return apply_parameters({{std::string(name), Vector2(value.x, value.y)}});
    }

    bool MaterialInstance::set_vector(std::string_view name, const vec3& value)
    {
        return apply_parameters({{std::string(name), Vector3(value.x, value.y, value.z)}});
    }

    bool MaterialInstance::set_vector(std::string_view name, const vec4& value)
    {
        return apply_parameters({{std::string(name), Vector4(value.x, value.y, value.z, value.w)}});
    }

    bool MaterialInstance::set_texture(std::string_view name, TextureRef texture)
    {
        return apply_parameters({{std::string(name), std::move(texture)}});
    }

    bool MaterialInstance::set_sampler(std::string_view name, MaterialSamplerPreset preset)
    {
        return apply_parameters({{std::string(name), preset}});
    }

    bool MaterialInterface::validate_parameters(const MaterialParameterChanges& changes) const
    {
        std::vector<ResolvedMaterialParameter> resolved;
        if (!resolve_material_changes(desc_, changes, resolved))
        {
            TOY_LOG_ERROR("Material parameter batch contains duplicate, unknown, incompatible or non-finite values.");
            return false;
        }
        return true;
    }

    bool MaterialInterface::overrides_parameter(std::string_view name) const
    {
        return std::any_of(local_overrides_.begin(), local_overrides_.end(),
                           [name](const MaterialParameterChange& value)
                           {
                               return value.name == name;
                           });
    }

    std::vector<MaterialStaticOption> MaterialInterface::effective_static_options() const
    {
        const auto source = parent();
        return merge_material_static_options(
            source ? source->effective_static_options() : std::vector<MaterialStaticOption>{}, desc_.static_options);
    }

    bool MaterialInterface::parameter_value(std::string_view name, MaterialParameterValue& output) const
    {
        for (const auto& value : local_overrides_)
        {
            if (value.name == name)
            {
                std::vector<ResolvedMaterialParameter> resolved;
                if (resolve_material_changes(desc_, {value}, resolved))
                {
                    output = value.value;
                    return true;
                }
            }
        }
        const auto source = parent();
        if (source)
        {
            return source->parameter_value(name, output);
        }
        std::vector<ResolvedMaterialParameter> resolved;
        if (!resolve_material_changes(desc_, {{std::string(name), std::monostate{}}}, resolved))
        {
            return false;
        }
        output = resolved.front().value;
        return true;
    }

    bool MaterialInterface::apply_parameters(const MaterialParameterChanges& changes)
    {
        if (!resolve_material_replacement_publication() || !validate_parameters(changes))
        {
            return false;
        }
        if (changes.empty())
        {
            return true;
        }
        auto next = local_overrides_;
        for (const auto& value : changes)
        {
            next.erase(std::remove_if(next.begin(), next.end(),
                                      [&value](const MaterialParameterChange& old)
                                      {
                                          return old.name == value.name;
                                      }),
                       next.end());
            // C++17 monostate means remove this layer's value, not copy Parent.
            if (!std::holds_alternative<std::monostate>(value.value))
            {
                next.push_back(value);
            }
        }
        return publish_tree(desc_, std::move(next));
    }

    bool MaterialInterface::publish_tree(MaterialDesc desc, MaterialParameterChanges overrides)
    {
        return publish_configurations({{this, std::move(desc), std::move(overrides), parent()}});
    }

    bool MaterialInterface::prepare_configurations(std::vector<Configuration> configurations,
                                                   const std::vector<ShaderMapCollectionRef>& shader_family,
                                                   std::vector<PreparedConfiguration>& prepared,
                                                   std::vector<MaterialInstanceRef>& owners)
    {
        std::map<const MaterialInterface*, Configuration> inputs;
        std::function<void(MaterialInterface*)> collect = [&](MaterialInterface* target)
        {
            if (inputs.count(target))
            {
                return;
            }
            inputs.emplace(target, Configuration{target, target->desc_, target->local_overrides_, target->parent()});
            for (auto it = target->children_.begin(); it != target->children_.end();)
            {
                auto child = it->lock();
                if (!child)
                {
                    it = target->children_.erase(it);
                    continue;
                }
                ++it;
                owners.push_back(child);
                collect(child.get());
            }
        };
        for (const auto& configuration : configurations)
        {
            collect(configuration.target);
        }
        for (auto& configuration : configurations)
        {
            inputs.at(configuration.target) = std::move(configuration);
        }
        auto revisions = std::make_shared<std::vector<PreparedConfiguration>>();
        revisions->reserve(inputs.size());
        std::map<const MaterialInterface*, std::size_t> positions;
        std::set<const MaterialInterface*> visiting;
        std::function<bool(MaterialInterface*)> resolve = [&](MaterialInterface* target) -> bool
        {
            if (positions.count(target))
            {
                return true;
            }
            if (!visiting.insert(target).second)
            {
                return false;
            }
            PreparedConfiguration revision;
            revision.configuration = inputs.at(target);
            revision.previously_used = target->render_proxy_used_;
            if (const auto* child = dynamic_cast<const MaterialInstance*>(target))
            {
                revision.previous_root = child->material_;
            }
            auto source = revision.configuration.parent;
            if (source)
            {
                const auto selected_map = revision.configuration.descriptor.shader_map;
                const auto local_options = revision.configuration.descriptor.static_options;
                auto inherited_options = source->effective_static_options();
                auto found = inputs.find(source.get());
                if (found != inputs.end())
                {
                    if (!resolve(found->second.target))
                    {
                        return false;
                    }
                    const auto& parent_revision = revisions->at(positions.at(source.get()));
                    revision.depth = parent_revision.depth + 1u;
                    revision.configuration.descriptor = parent_revision.configuration.descriptor;
                    revision.effective = parent_revision.effective;
                    revision.root = parent_revision.root;
                    inherited_options = parent_revision.static_options;
                }
                else
                {
                    for (auto ancestor = source; ancestor; ancestor = ancestor->parent())
                    {
                        if (++revision.depth > maximum_material_parent_depth)
                        {
                            return false;
                        }
                    }
                    revision.configuration.descriptor = source->desc();
                    revision.effective = source->desc();
                    for (const auto& buffer : revision.effective.parameter_schema.constant_buffers)
                    {
                        for (const auto& member : buffer.members)
                        {
                            MaterialParameterValue value;
                            if (!source->parameter_value(member.name, value))
                            {
                                return false;
                            }
                            // C++17 get_if maps inherited values into owned candidates.
                            if (const auto* number = std::get_if<float>(&value))
                            {
                                revision.effective.scalar_defaults[member.parameter_id] = *number;
                            }
                            else if (const auto* vector = std::get_if<Vector2>(&value))
                            {
                                revision.effective.vector2_defaults[member.parameter_id] = vec2(vector->x, vector->y);
                            }
                            else if (const auto* vector = std::get_if<Vector3>(&value))
                            {
                                revision.effective.vector3_defaults[member.parameter_id] =
                                    vec3(vector->x, vector->y, vector->z);
                            }
                            else if (const auto* vector = std::get_if<Vector4>(&value))
                            {
                                revision.effective.vector4_defaults[member.parameter_id] =
                                    vec4(vector->x, vector->y, vector->z, vector->w);
                            }
                        }
                    }
                    for (const auto& resource : revision.effective.parameter_schema.resources)
                    {
                        MaterialParameterValue value;
                        if (!source->parameter_value(resource.name, value))
                        {
                            return false;
                        }
                        if (const auto* texture = std::get_if<TextureRef>(&value))
                        {
                            revision.effective.texture_defaults[resource.parameter_id] = *texture;
                        }
                        else if (const auto* sampler = std::get_if<MaterialSamplerPreset>(&value))
                        {
                            revision.effective.sampler_defaults[resource.parameter_id] = *sampler;
                        }
                        else
                        {
                            return false;
                        }
                    }
                }
                if (!revision.root)
                {
                    revision.root = std::dynamic_pointer_cast<const Material>(source);
                    if (!revision.root)
                    {
                        revision.root = std::dynamic_pointer_cast<const MaterialInstance>(source)->material();
                    }
                }
                revision.configuration.descriptor.static_options = local_options;
                revision.static_options = merge_material_static_options(inherited_options, local_options);
                auto selected = revision.configuration.descriptor.shader_map;
                if (selected)
                {
                    const auto resolved = shader::resolve_shader_permutation(
                        selected->index().material_domain, material_static_selections(revision.static_options));
                    if (!resolved.succeeded())
                    {
                        return false;
                    }
                    if (!shader_family.empty())
                    {
                        const auto found = std::find_if(shader_family.begin(), shader_family.end(),
                                                        [&](const ShaderMapCollectionRef& candidate)
                                                        {
                                                            return candidate && candidate->index().permutation_key ==
                                                                                    resolved.permutation->key;
                                                        });
                        if (found == shader_family.end())
                        {
                            return false;
                        }
                        selected = *found;
                    }
                    else if (revision.configuration.shader_configuration_selected)
                    {
                        selected = selected_map;
                    }
                    else if (selected->index().permutation_key != resolved.permutation->key)
                    {
                        selected = selected_map;
                    }
                    if (!selected || selected->index().permutation_key != resolved.permutation->key ||
                        selected->index().shader_name != revision.configuration.descriptor.shader_name ||
                        material_parameter_schema_from_shader_schema(
                            selected->programs().front()->data().parameter_schema)
                                .schema_identity != revision.configuration.descriptor.parameter_schema.schema_identity)
                    {
                        return false;
                    }
                    revision.configuration.descriptor.shader_map = selected;
                    revision.effective.shader_map = selected;
                }
            }
            else
            {
                revision.effective = revision.configuration.descriptor;
                revision.static_options = revision.configuration.descriptor.static_options;
            }
            if (revision.depth > maximum_material_parent_depth)
            {
                return false;
            }
            for (const auto& value : revision.configuration.overrides)
            {
                std::vector<ResolvedMaterialParameter> resolved;
                // Old-schema orphans stay in the authoring configuration.
                if (!resolve_material_changes(revision.configuration.descriptor, {value}, resolved))
                {
                    continue;
                }
                const auto& item = resolved.front();
                // get_if keeps the finite runtime branches explicit.
                if (const auto* number = std::get_if<float>(&item.value))
                {
                    revision.effective.scalar_defaults[item.id] = *number;
                }
                else if (const auto* vector = std::get_if<Vector2>(&item.value))
                {
                    revision.effective.vector2_defaults[item.id] = vec2(vector->x, vector->y);
                }
                else if (const auto* vector = std::get_if<Vector3>(&item.value))
                {
                    revision.effective.vector3_defaults[item.id] = vec3(vector->x, vector->y, vector->z);
                }
                else if (const auto* vector = std::get_if<Vector4>(&item.value))
                {
                    revision.effective.vector4_defaults[item.id] = vec4(vector->x, vector->y, vector->z, vector->w);
                }
                else if (const auto* texture = std::get_if<TextureRef>(&item.value))
                {
                    revision.effective.texture_defaults[item.id] = *texture;
                }
                else if (const auto* sampler = std::get_if<MaterialSamplerPreset>(&item.value))
                {
                    revision.effective.sampler_defaults[item.id] = *sampler;
                }
            }
            revision.destination = target->material_render_proxy_.get();
            if (!revision.destination)
            {
                return false;
            }
            revision.proxy = std::make_shared<MaterialRenderProxy>(revision.effective);
            positions[target] = revisions->size();
            revisions->push_back(std::move(revision));
            visiting.erase(target);
            return true;
        };
        for (const auto& input : inputs)
        {
            if (!resolve(input.second.target))
            {
                return false;
            }
        }
        prepared = std::move(*revisions);
        return true;
    }

    bool MaterialInterface::publish_configurations(std::vector<Configuration> configurations,
                                                   const std::vector<ShaderMapCollectionRef>& shader_family)
    {
        auto revisions = std::make_shared<std::vector<PreparedConfiguration>>();
        std::vector<MaterialInstanceRef> owners;
        if (!prepare_configurations(std::move(configurations), shader_family, *revisions, owners))
        {
            return false;
        }
        auto previous = std::make_shared<std::vector<Configuration>>();
        for (const auto& revision : *revisions)
        {
            auto* target = revision.configuration.target;
            previous->push_back({target, target->desc_, target->local_overrides_, target->parent()});
            if (revision.configuration.parent != target->parent())
            {
                if (const auto old_parent = target->parent())
                {
                    old_parent->children_.reserve(old_parent->children_.size() + revisions->size());
                }
                if (revision.configuration.parent)
                {
                    revision.configuration.parent->children_.reserve(revision.configuration.parent->children_.size() +
                                                                     revisions->size());
                }
            }
        }
        const auto commit = [](Configuration& configuration, MaterialRef& root)
        {
            auto* target = configuration.target;
            target->desc_ = std::move(configuration.descriptor);
            target->local_overrides_ = std::move(configuration.overrides);
            target->shader_map_ = target->desc_.shader_map;
            target->two_sided_ = target->desc_.two_sided;
            target->render_proxy_used_ = true;
            auto* child = dynamic_cast<MaterialInstance*>(target);
            if (child)
            {
                if (child->parent_ != configuration.parent)
                {
                    auto& old_children = child->parent_->children_;
                    std::weak_ptr<MaterialInstance> self;
                    for (const auto& old : old_children)
                    {
                        const auto value = old.lock();
                        if (value.get() == child)
                        {
                            self = old;
                            break;
                        }
                    }
                    old_children.erase(std::remove_if(old_children.begin(), old_children.end(),
                                                      [child](const std::weak_ptr<MaterialInstance>& old)
                                                      {
                                                          const auto value = old.lock();
                                                          return !value || value.get() == child;
                                                      }),
                                       old_children.end());
                    child->parent_ = std::move(configuration.parent);
                    child->parent_->children_.push_back(std::move(self));
                }
                if (root)
                {
                    child->material_ = std::move(root);
                }
            }
        };
        // Publish GT strong references before admission. This is essential when
        // single-thread mode executes the RT body inline: old TextureRefs must
        // already live solely in the owned retirement payload at that point.
        for (auto& revision : *revisions)
        {
            commit(revision.configuration, revision.root);
        }
        try
        {
            enqueue_render_command("PublishMaterialHierarchy",
                                   [revisions, previous]() noexcept
                                   {
                                       for (auto& revision : *revisions)
                                       {
                                           revision.destination->replace_state(std::move(*revision.proxy));
                                       }
                                       for (auto& old : *previous)
                                       {
                                           release_material_textures(old.descriptor, old.overrides);
                                       }
                                   });
        }
        catch (...)
        {
            for (std::size_t i = 0; i < revisions->size(); ++i)
            {
                commit(previous->at(i), revisions->at(i).previous_root);
                previous->at(i).target->render_proxy_used_ = revisions->at(i).previously_used;
            }
            throw;
        }
        return true;
    }

    bool MaterialInstance::reset_parameter(std::string_view name)
    {
        return apply_parameters({{std::string(name), std::monostate{}}});
    }

    bool MaterialInterface::stage_material_replacement(ShaderMapCollectionRef shader_map, bool two_sided)
    {
        if (!resolve_material_replacement_publication() || !shader_map || pending_shader_map_)
        {
            TOY_LOG_ERROR("Material ShaderMap replacement requires one complete candidate.");
            return false;
        }
        std::string error;
        if (!is_shader_map_compatible_with_material(desc_, *shader_map, error))
        {
            TOY_LOG_ERROR("Invalid Material ShaderMap candidate: {}.", error);
            return false;
        }

        pending_shader_map_ = shader_map;
        pending_two_sided_ = two_sided;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("StageMaterialCandidate",
                               [proxy, shader_map = std::move(shader_map), two_sided]() noexcept
                               {
                                   const RHIStatus status =
                                       proxy->stage_material_candidate(std::move(shader_map), two_sided);
                                   if (!status)
                                   {
                                       TOY_LOG_ERROR("Material candidate staging failed: {}", status.message());
                                   }
                               });
        return true;
    }

    bool MaterialInterface::publish_material_replacement()
    {
        if (!resolve_material_replacement_publication() || !pending_shader_map_)
        {
            TOY_LOG_ERROR("Material has no ShaderMap candidate to publish.");
            return false;
        }
        replacement_commit_succeeded_->store(false, std::memory_order_relaxed);
        replacement_commit_complete_->store(false, std::memory_order_relaxed);
        replacement_publication_pending_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        const std::shared_ptr<std::atomic<bool>> commit_complete = replacement_commit_complete_;
        const std::shared_ptr<std::atomic<bool>> commit_succeeded = replacement_commit_succeeded_;
        try
        {
            enqueue_render_command("PublishMaterialCandidate",
                                   [proxy, commit_complete, commit_succeeded]() noexcept
                                   {
                                       const RHIStatus status = proxy->commit_material_candidate();
                                       if (!status)
                                       {
                                           TOY_LOG_ERROR("Material candidate commit failed: {}", status.message());
                                       }
                                       commit_succeeded->store(status.succeeded(), std::memory_order_relaxed);
                                       // Release publishes both the RT commit result and the complete
                                       // active-state mutation before GT resolves its Shader schema.
                                       commit_complete->store(true, std::memory_order_release);
                                   });
        }
        catch (...)
        {
            replacement_publication_pending_ = false;
            throw;
        }
        return true;
    }

    bool MaterialInterface::discard_material_replacement()
    {
        if (!resolve_material_replacement_publication() || !pending_shader_map_)
        {
            return false;
        }
        pending_shader_map_.reset();
        pending_two_sided_ = two_sided_;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("DiscardMaterialCandidate",
                               [proxy]() noexcept
                               {
                                   proxy->discard_material_candidate();
                               });
        return true;
    }

    bool MaterialInterface::resolve_material_replacement_publication()
    {
        if (!replacement_publication_pending_)
        {
            return true;
        }
        // Acquire makes the RT commit and its result visible before GT changes
        // the parameter-validation schema associated with the active Program.
        if (!replacement_commit_complete_->load(std::memory_order_acquire))
        {
            TOY_LOG_ERROR("Material replacement publication is still pending on the Rendering Thread.");
            return false;
        }

        const bool commit_succeeded = replacement_commit_succeeded_->load(std::memory_order_relaxed);
        if (commit_succeeded)
        {
            shader_map_ = pending_shader_map_;
            two_sided_ = pending_two_sided_;
            desc_.shader_map = shader_map_;
            desc_.two_sided = two_sided_;
        }
        pending_shader_map_.reset();
        pending_two_sided_ = two_sided_;
        replacement_publication_pending_ = false;
        return true;
    }

    MaterialRenderProxy* MaterialInterface::material_render_proxy() const noexcept
    {
        render_proxy_used_ = true;
        return material_render_proxy_.get();
    }
} // namespace toy3d
