#include "rendercore/material/material_render_proxy.h"

#include "drivers/rhi/rhi_command_context.h"
#include "logging/logger.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/shader_parameters.h"
#include "rendercore/render_resource.h"
#include "rendercore/render_resource_manager.h"
#include "rendercore/texture/texture_resource.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        ShaderParametersMetadata make_material_parameter_metadata(const shader::ShaderParameterSchema& schema)
        {
            ShaderParametersMetadata metadata;
            metadata.group = shader::BindingGroup::Material;
            metadata.generated_format_version = schema.generated_format_version;
            metadata.shader_abi_version = schema.shader_abi_version;
            metadata.parameter_id_version = schema.parameter_id_version;
            metadata.cpp_identifier_version = shader::shader_parameters_cpp_identifier_version;
            metadata.schema_identity = schema.schema_identity;
            metadata.editor_properties_hash = schema.editor_properties_hash;
            metadata.group_identity =
                shader::calculate_shader_parameter_group_identity(schema, shader::BindingGroup::Material);

            if (!schema.constant_buffers.empty())
            {
                const shader::ShaderParameterConstantBufferSchema& schema_buffer = schema.constant_buffers.front();
                metadata.constant_buffer.binding_id = schema_buffer.binding_id;
                metadata.constant_buffer.size = schema_buffer.size;
                metadata.constant_buffer.data_layout_hash = schema_buffer.data_layout_hash;
                metadata.constant_buffer.shader_abi_version = schema_buffer.shader_abi_version;
                metadata.constant_buffer.name = schema_buffer.name;
                metadata.constant_buffer.members.reserve(schema_buffer.members.size());
                for (const shader::ShaderParameterConstantMemberSchema& member : schema_buffer.members)
                {
                    metadata.constant_buffer.members.push_back({member.parameter_id, member.type, member.offset,
                                                                member.size, member.array_count, member.array_stride,
                                                                member.matrix_stride, member.default_value, member.name,
                                                                member.minimum_value, member.maximum_value});
                }
            }

            metadata.resources.reserve(schema.resources.size());
            for (const shader::ShaderParameterResourceSchema& resource : schema.resources)
            {
                metadata.resources.push_back({resource.parameter_id, resource.category, resource.resource_kind,
                                              resource.element_type, resource.array_count, resource.default_value_kind,
                                              resource.default_value, resource.name, resource.texture_usage});
            }
            return metadata;
        }

        RHIStatus write_material_constant(const ShaderParameterConstantMemberMetadata& member,
                                          const std::unordered_map<ShaderParameterId, float>& scalars,
                                          const std::unordered_map<ShaderParameterId, vec2>& vectors2,
                                          const std::unordered_map<ShaderParameterId, vec3>& vectors3,
                                          const std::unordered_map<ShaderParameterId, vec4>& vectors4,
                                          ShaderParameterEncoder& encoder)
        {
            switch (member.type)
            {
            case shader::ShaderValueType::Float32:
            {
                const auto found = scalars.find(member.parameter_id);
                if (found == scalars.end())
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Material scalar value is missing");
                }
                encoder.write_constant(member, found->second);
                break;
            }
            case shader::ShaderValueType::Float32x2:
            {
                const auto found = vectors2.find(member.parameter_id);
                if (found == vectors2.end())
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Material float2 value is missing");
                }
                encoder.write_constant(member, Vector2(found->second.x, found->second.y));
                break;
            }
            case shader::ShaderValueType::Float32x3:
            {
                const auto found = vectors3.find(member.parameter_id);
                if (found == vectors3.end())
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Material float3 value is missing");
                }
                encoder.write_constant(member, Vector3(found->second.x, found->second.y, found->second.z));
                break;
            }
            case shader::ShaderValueType::Float32x4:
            {
                const auto found = vectors4.find(member.parameter_id);
                if (found == vectors4.end())
                {
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Material float4 value is missing");
                }
                encoder.write_constant(member,
                                       Vector4(found->second.x, found->second.y, found->second.z, found->second.w));
                break;
            }
            default:
                return RHIStatus::failure(RHIErrorCode::Unsupported,
                                          "Material constant type is not supported by the first-stage proxy");
            }
            return encoder.succeeded() ? RHIStatus::success()
                                       : RHIStatus::failure(RHIErrorCode::InvalidArgument, encoder.error());
        }

        RHIStatus begin_init_texture_resource(TextureResource& resource, RenderResourceManager& manager)
        {
            switch (resource.state())
            {
            case RenderResourceState::Uninitialized:
                return resource.begin_init(manager);
            case RenderResourceState::PendingUpload:
            case RenderResourceState::Ready:
                return RHIStatus::success();
            case RenderResourceState::Failed:
                return resource.failure_status();
            case RenderResourceState::Released:
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Material texture resource was already released");
            }
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Material texture resource has an unknown state");
        }
        RHISamplerDesc material_sampler_desc(MaterialSamplerPreset preset)
        {
            RHISamplerDesc desc;
            const bool point =
                preset == MaterialSamplerPreset::PointClamp || preset == MaterialSamplerPreset::PointWrap;
            const bool trilinear =
                preset == MaterialSamplerPreset::TrilinearClamp || preset == MaterialSamplerPreset::TrilinearWrap;
            const bool wrap = preset == MaterialSamplerPreset::PointWrap ||
                              preset == MaterialSamplerPreset::LinearWrap ||
                              preset == MaterialSamplerPreset::TrilinearWrap;
            desc.min_filter = point ? RHIFilter::Nearest : RHIFilter::Linear;
            desc.mag_filter = desc.min_filter;
            desc.mip_filter = trilinear ? RHIFilter::Linear : RHIFilter::Nearest;
            desc.address_u = desc.address_v = desc.address_w =
                wrap ? RHIAddressMode::Repeat : RHIAddressMode::ClampToEdge;
            desc.debug_name = "MaterialSampler";
            return desc;
        }
    } // namespace

    MaterialRenderProxy::MaterialRenderProxy(const Material& material) : MaterialRenderProxy(material.desc())
    {
    }

    MaterialRenderProxy::MaterialRenderProxy(const MaterialDesc& desc)
        : shader_name_(desc.shader_name), parameter_schema_(desc.parameter_schema),
          parameter_metadata_(make_material_parameter_metadata(parameter_schema_)), shader_map_(desc.shader_map),
          two_sided_(desc.two_sided)
    {
        scalar_parameters_ = desc.scalar_defaults;
        vector2_parameters_ = desc.vector2_defaults;
        vector3_parameters_ = desc.vector3_defaults;
        vector4_parameters_ = desc.vector4_defaults;
        for (const auto& default_texture : desc.texture_defaults)
        {
            texture_parameters_[default_texture.first] =
                default_texture.second ? default_texture.second->texture_resource() : nullptr;
        }
        sampler_parameters_ = desc.sampler_defaults;
    }

    void MaterialRenderProxy::replace_state(MaterialRenderProxy&& candidate) noexcept
    {
        // A code-only publication keeps the logical binding independent from
        // Program/native mappings when schema and all effective values match.
        if (parameter_schema_.schema_identity == candidate.parameter_schema_.schema_identity)
        {
            candidate.bindings_ = std::move(bindings_);
            const bool constants_match = scalar_parameters_ == candidate.scalar_parameters_ &&
                                         vector2_parameters_ == candidate.vector2_parameters_ &&
                                         vector3_parameters_ == candidate.vector3_parameters_ &&
                                         vector4_parameters_ == candidate.vector4_parameters_;
            for (auto& item : candidate.bindings_)
            {
                auto& cache = item.second;
                cache.dirty = cache.dirty || (cache.metadata.constant_buffer.size != 0u && !constants_match);
                for (const auto& resource : cache.metadata.resources)
                {
                    bool values_match = false;
                    if (resource.category == shader::ShaderParameterCategory::Sampler)
                    {
                        const auto previous = sampler_parameters_.find(resource.parameter_id);
                        const auto next = candidate.sampler_parameters_.find(resource.parameter_id);
                        values_match = previous != sampler_parameters_.end() &&
                                       next != candidate.sampler_parameters_.end() && previous->second == next->second;
                    }
                    else
                    {
                        const auto previous = texture_parameters_.find(resource.parameter_id);
                        const auto next = candidate.texture_parameters_.find(resource.parameter_id);
                        values_match = previous != texture_parameters_.end() &&
                                       next != candidate.texture_parameters_.end() && previous->second == next->second;
                    }
                    cache.dirty = cache.dirty || !values_match;
                }
            }
            const auto retained = candidate.retain_configuration_bindings(candidate.shader_map_, candidate.bindings_);
            if (!retained)
            {
                candidate.bindings_.clear();
                TOY_LOG_ERROR("Material candidate binding cache admission failed: {}", retained.message());
            }
        }
        candidate.sampler_cache_ = std::move(sampler_cache_);
        candidate.resource_manager_ = resource_manager_;
        if (resource_manager_ != nullptr)
        {
            const auto status = candidate.begin_init_textures(*resource_manager_);
            if (!status)
            {
                TOY_LOG_ERROR("Material candidate texture initialization failed: {}", status.message());
            }
        }
        *this = std::move(candidate);
    }

    void MaterialRenderProxy::apply_scalar_update(ShaderParameterId parameter_id, float value) noexcept
    {
        const auto current = scalar_parameters_.find(parameter_id);
        if (current != scalar_parameters_.end() && current->second == value)
        {
            return;
        }
        scalar_parameters_[parameter_id] = value;
        invalidate_parameter(parameter_id, true);
    }

    void MaterialRenderProxy::apply_vector_update(ShaderParameterId parameter_id, const vec2& value) noexcept
    {
        const auto current = vector2_parameters_.find(parameter_id);
        if (current != vector2_parameters_.end() && current->second.x == value.x && current->second.y == value.y)
        {
            return;
        }
        vector2_parameters_[parameter_id] = value;
        invalidate_parameter(parameter_id, true);
    }

    void MaterialRenderProxy::apply_vector_update(ShaderParameterId parameter_id, const vec3& value) noexcept
    {
        const auto current = vector3_parameters_.find(parameter_id);
        if (current != vector3_parameters_.end() && current->second.x == value.x && current->second.y == value.y &&
            current->second.z == value.z)
        {
            return;
        }
        vector3_parameters_[parameter_id] = value;
        invalidate_parameter(parameter_id, true);
    }

    void MaterialRenderProxy::apply_vector_update(ShaderParameterId parameter_id, const vec4& value) noexcept
    {
        const auto current = vector4_parameters_.find(parameter_id);
        if (current != vector4_parameters_.end() && current->second.x == value.x && current->second.y == value.y &&
            current->second.z == value.z && current->second.w == value.w)
        {
            return;
        }
        vector4_parameters_[parameter_id] = value;
        invalidate_parameter(parameter_id, true);
    }

    void MaterialRenderProxy::apply_texture_update(ShaderParameterId parameter_id,
                                                   TextureResource* texture_resource) noexcept
    {
        const auto current = texture_parameters_.find(parameter_id);
        if (current != texture_parameters_.end() && current->second == texture_resource)
        {
            return;
        }
        texture_parameters_[parameter_id] = texture_resource;
        if (resource_manager_ != nullptr && texture_resource != nullptr &&
            ((shader_map_ && material_parameter_is_active(*shader_map_, parameter_id)) ||
             (staged_shader_map_ && material_parameter_is_active(*staged_shader_map_, parameter_id))))
        {
            const RHIStatus status = begin_init_texture_resource(*texture_resource, *resource_manager_);
            if (!status)
            {
                TOY_LOG_ERROR("MaterialRenderProxy could not initialize a TextureResource update: {}",
                              status.message());
            }
        }
        invalidate_parameter(parameter_id, false);
    }

    RHIStatus MaterialRenderProxy::begin_init_textures(RenderResourceManager& manager)
    {
        resource_manager_ = &manager;
        for (const auto& texture_parameter : texture_parameters_)
        {
            if (shader_map_ && !material_parameter_is_active(*shader_map_, texture_parameter.first) &&
                (!staged_shader_map_ || !material_parameter_is_active(*staged_shader_map_, texture_parameter.first)))
            {
                continue;
            }
            TextureResource* const resource = texture_parameter.second;
            if (resource == nullptr)
            {
                return RHIStatus::failure(RHIErrorCode::NotReady,
                                          "Active Material texture parameter has no TextureResource");
            }
            const RHIStatus status = begin_init_texture_resource(*resource, manager);
            if (!status)
            {
                return status;
            }
        }
        return RHIStatus::success();
    }

    void MaterialRenderProxy::invalidate_parameter(ShaderParameterId parameter_id, bool constant) noexcept
    {
        for (auto* caches : {&bindings_, &staged_bindings_})
        {
            for (auto& item : *caches)
            {
                auto& cached = item.second;
                const bool used = constant
                                      ? cached.metadata.constant_buffer.size != 0u
                                      : std::any_of(cached.metadata.resources.begin(), cached.metadata.resources.end(),
                                                    [parameter_id](const auto& resource)
                                                    {
                                                        return resource.parameter_id == parameter_id;
                                                    });
                cached.dirty = cached.dirty || used;
            }
        }
        staged_materialized_ = false;
    }

    bool MaterialRenderProxy::texture_cache_matches(const MaterialBindingCache& binding,
                                                    bool check_generation) const noexcept
    {
        if (binding.texture_generations.size() != binding.texture_views.size())
        {
            return false;
        }
        for (const auto& generation : binding.texture_generations)
        {
            TextureResource* const resource = generation.first;
            const auto view = binding.texture_views.find(resource);
            if (resource == nullptr || view == binding.texture_views.end() ||
                (check_generation && generation.second != resource->binding_generation()) ||
                view->second != resource->view_for_current_recording())
            {
                return false;
            }
        }
        return true;
    }

    RHIStatus MaterialRenderProxy::retain_configuration_bindings(
        const ShaderMapCollectionRef& shader_map, std::map<Sha256Hash, MaterialBindingCache>& bindings) const
    {
        if (!shader_map)
        {
            bindings.clear();
            return RHIStatus::success();
        }
        std::set<Sha256Hash> required;
        for (const auto& program : shader_map->programs())
        {
            if (std::none_of(program->data().bindings.begin(), program->data().bindings.end(),
                             [](const auto& binding)
                             {
                                 return binding.group == RHIBindingGroup::Material;
                             }))
            {
                continue;
            }
            const auto metadata = shader_parameters_metadata_for_program(parameter_metadata_, program->data());
            if (!metadata)
            {
                return metadata.status();
            }
            required.insert(metadata.value().group_identity);
        }
        for (auto item = bindings.begin(); item != bindings.end();)
        {
            if (required.count(item->first) == 0u)
            {
                item = bindings.erase(item);
            }
            else
            {
                ++item;
            }
        }
        return RHIStatus::success();
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize(RHIDevice& device, RHICommandContext& context)
    {
        return materialize_configuration(device, context, shader_map_, false);
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize(RHIDevice& device, RHICommandContext& context,
                                                                 const ShaderMapProgram& program)
    {
        return materialize_program(device, context, shader_map_, program, false);
    }

    RHIStatus MaterialRenderProxy::stage_material_candidate(ShaderMapCollectionRef shader_map, bool two_sided)
    {
        if (!shader_map || staged_shader_map_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "MaterialRenderProxy requires exactly one complete staged ShaderMap collection");
        }
        if (shader_map->index().shader_name != shader_name_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Staged ShaderMap collection does not match the Material identity");
        }
        const shader::ShaderParameterSchema candidate_schema =
            material_parameter_schema_from_shader_schema(shader_map->programs().front()->data().parameter_schema);
        if (candidate_schema.schema_identity != parameter_schema_.schema_identity)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Staged ShaderMap collection has a different complete Material schema");
        }

        staged_shader_map_ = std::move(shader_map);
        staged_two_sided_ = two_sided;
        // Copy only equivalent active groups. Candidate preparation cannot
        // mutate current bindings, and obsolete layouts do not accumulate.
        staged_bindings_ = bindings_;
        const auto retained = retain_configuration_bindings(staged_shader_map_, staged_bindings_);
        if (!retained)
        {
            discard_material_candidate();
            return retained;
        }
        if (resource_manager_ != nullptr)
        {
            const auto initialized = begin_init_textures(*resource_manager_);
            if (!initialized)
            {
                discard_material_candidate();
                return initialized;
            }
        }
        staged_materialized_ = false;
        return RHIStatus::success();
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_staged(RHIDevice& device, RHICommandContext& context)
    {
        if (!staged_shader_map_)
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::NotReady, "No Material candidate is staged");
        }
        // Publication preflights every required role/factory/Pass configuration,
        // while normal draw preparation requests only its selected Program.
        for (const auto& program : staged_shader_map_->programs())
        {
            const auto created = materialize_program(device, context, staged_shader_map_, *program, true);
            if (!created)
            {
                staged_materialized_ = false;
                return created;
            }
        }
        auto result = materialize_configuration(device, context, staged_shader_map_, true);
        staged_materialized_ = result.succeeded();
        return result;
    }

    RHIStatus MaterialRenderProxy::commit_material_candidate()
    {
        bool current = staged_shader_map_ && staged_materialized_;
        for (const auto& item : staged_bindings_)
        {
            current =
                current && item.second.binding_set && !item.second.dirty && texture_cache_matches(item.second, false);
        }
        if (!current)
        {
            const RHIStatus status = RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Material candidate bindings must be current and fully materialized before submit commit");
            discard_material_candidate();
            return status;
        }
        // Resource upload commit may advance generations without changing the
        // candidate views used by this submitted recording.
        for (auto& item : staged_bindings_)
        {
            for (auto& generation : item.second.texture_generations)
            {
                generation.second = generation.first->binding_generation();
            }
        }
        shader_map_ = std::move(staged_shader_map_);
        two_sided_ = staged_two_sided_;
        bindings_ = std::move(staged_bindings_);
        staged_materialized_ = false;
        return RHIStatus::success();
    }

    void MaterialRenderProxy::discard_material_candidate() noexcept
    {
        staged_shader_map_.reset();
        staged_two_sided_ = false;
        staged_bindings_.clear();
        staged_materialized_ = false;
    }

    shader::ShaderGraphicsPassState MaterialRenderProxy::effective_graphics_pass_state(
        const ShaderMapProgram& program) const noexcept
    {
        auto state = program.data().graphics_pass_state;
        if (two_sided_)
        {
            state.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
        }
        return state;
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_configuration(RHIDevice& device,
                                                                               RHICommandContext& context,
                                                                               const ShaderMapCollectionRef& shader_map,
                                                                               bool staged)
    {
        if (!shader_map)
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::NotReady,
                                                        "Material binding requires a ShaderMap collection");
        }
        const auto factory = (shader_map->programs().front()->data().contract.vertex_factory_support &
                              shader::local_vertex_factory_support) != 0u
                                 ? shader::VertexFactoryType::Local
                                 : shader::VertexFactoryType::GPUSkin;
        const auto selected = shader_map->find(shader::ShaderPassRole::Forward, factory);
        if (!selected.succeeded())
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::NotReady, selected.error);
        }
        return materialize_program(device, context, shader_map, *selected.program, staged);
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_program(RHIDevice& device, RHICommandContext& context,
                                                                         const ShaderMapCollectionRef& shader_map,
                                                                         const ShaderMapProgram& program, bool staged)
    {
        if (!shader_map || std::none_of(shader_map->programs().begin(), shader_map->programs().end(),
                                        [&program](const auto& known)
                                        {
                                            return known.get() == &program;
                                        }))
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument,
                                                        "Material binding Program is outside its configuration");
        }
        if (std::none_of(program.data().bindings.begin(), program.data().bindings.end(),
                         [](const auto& binding)
                         {
                             return binding.group == RHIBindingGroup::Material;
                         }))
        {
            return RHIResult<RHIBindingSetRef>::success(nullptr);
        }
        const RHIStatus metadata_status =
            validate_shader_parameters_metadata_against_schema(parameter_metadata_, parameter_schema_);
        if (!metadata_status)
        {
            return RHIResult<RHIBindingSetRef>::failure(metadata_status.code(), metadata_status.message());
        }

        ShaderParameterEncoder encoder(parameter_metadata_, program.data());
        if (!encoder.succeeded())
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument, encoder.error());
        }
        const auto& metadata = encoder.binding_metadata();
        auto& caches = staged ? staged_bindings_ : bindings_;
        auto& cached = caches[metadata.group_identity];
        if (!cached.dirty && cached.binding_set && texture_cache_matches(cached, true))
        {
            return RHIResult<RHIBindingSetRef>::success(cached.binding_set);
        }
        for (const ShaderParameterConstantMemberMetadata& member : metadata.constant_buffer.members)
        {
            const RHIStatus status = write_material_constant(member, scalar_parameters_, vector2_parameters_,
                                                             vector3_parameters_, vector4_parameters_, encoder);
            if (!status)
            {
                return RHIResult<RHIBindingSetRef>::failure(status.code(), status.message());
            }
        }

        std::unordered_map<TextureResource*, std::uint64_t> generations;
        std::unordered_map<TextureResource*, RHITextureViewRef> views;
        for (const ShaderParameterResourceMetadata& resource_metadata : metadata.resources)
        {
            if (resource_metadata.category == shader::ShaderParameterCategory::Sampler)
            {
                const auto parameter = sampler_parameters_.find(resource_metadata.parameter_id);
                if (parameter == sampler_parameters_.end())
                {
                    return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument,
                                                                "Material Sampler value is missing");
                }
                if (parameter->second < MaterialSamplerPreset::PointClamp ||
                    parameter->second > MaterialSamplerPreset::TrilinearWrap)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::Unsupported, "Material Sampler preset is not supported by this binding");
                }
                auto cached = sampler_cache_.find(parameter->second);
                if (cached == sampler_cache_.end())
                {
                    auto created_sampler = device.create_sampler(material_sampler_desc(parameter->second));
                    if (!created_sampler)
                    {
                        return RHIResult<RHIBindingSetRef>::failure(created_sampler.status().code(),
                                                                    created_sampler.status().message());
                    }
                    cached = sampler_cache_.emplace(parameter->second, created_sampler.value()).first;
                }
                encoder.add_resource(resource_metadata, cached->second);
                if (!encoder.succeeded())
                {
                    return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument, encoder.error());
                }
                continue;
            }
            const auto parameter = texture_parameters_.find(resource_metadata.parameter_id);
            TextureResource* const resource = parameter != texture_parameters_.end() ? parameter->second : nullptr;
            const RHITextureViewRef view = resource != nullptr ? resource->view_for_current_recording() : nullptr;
            if (resource == nullptr || !view)
            {
                return RHIResult<RHIBindingSetRef>::failure(
                    RHIErrorCode::NotReady, "Material TextureResource has no view for the current recording");
            }
            const auto requirement =
                std::find_if(parameter_schema_.resources.begin(), parameter_schema_.resources.end(),
                             [&](const shader::ShaderParameterResourceSchema& item)
                             {
                                 return item.parameter_id == resource_metadata.parameter_id;
                             });
            if (requirement == parameter_schema_.resources.end() ||
                resource->usage_for_current_recording() != requirement->texture_usage)
            {
                return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument,
                                                            "Material texture usage differs from Shader requirement: " +
                                                                resource_metadata.name);
            }
            encoder.add_resource(resource_metadata, view);
            if (!encoder.succeeded())
            {
                return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument, encoder.error());
            }
            generations[resource] = resource->binding_generation();
            views[resource] = view;
        }

        RHIResult<RHIBindingSetRef> created =
            create_persistent_shader_binding(device, context, metadata, encoder, shader_name_ + " MaterialBindings");
        if (!created)
        {
            return created;
        }
        cached.metadata = metadata;
        cached.binding_set = created.value();
        cached.texture_generations = std::move(generations);
        cached.texture_views = std::move(views);
        cached.dirty = false;
        return RHIResult<RHIBindingSetRef>::success(cached.binding_set);
    }
} // namespace toy3d
