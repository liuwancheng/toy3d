#include "renderscene/material/material_render_proxy.h"

#include "drivers/rhi/rhi_command_context.h"
#include "logging/logger.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/shader_parameters.h"
#include "renderscene/render_resource.h"
#include "renderscene/render_resource_manager.h"
#include "renderscene/texture/texture_resource.h"

#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        ShaderParametersMetadata make_material_parameter_metadata(
            const shader::ShaderParameterSchema& schema)
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
                    metadata.constant_buffer.members.push_back(
                        {member.parameter_id, member.type, member.offset, member.size, member.array_count,
                         member.array_stride, member.matrix_stride, member.default_value, member.name});
                }
            }

            metadata.resources.reserve(schema.resources.size());
            for (const shader::ShaderParameterResourceSchema& resource : schema.resources)
            {
                metadata.resources.push_back(
                    {resource.parameter_id, resource.category, resource.resource_kind, resource.element_type,
                     resource.array_count, resource.default_value_kind, resource.default_value, resource.name});
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
                    return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Material scalar value is missing");
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
                encoder.write_constant(
                    member, Vector4(found->second.x, found->second.y, found->second.z, found->second.w));
                break;
            }
            default:
                return RHIStatus::failure(RHIErrorCode::Unsupported,
                                          "Material constant type is not supported by the first-stage proxy");
            }
            return encoder.succeeded()
                       ? RHIStatus::success()
                       : RHIStatus::failure(RHIErrorCode::InvalidArgument, encoder.error());
        }

        RHIStatus derive_effective_graphics_pass_state(const ShaderMapProgramRef& shader_program, bool two_sided,
                                                       shader::ShaderGraphicsPassState& effective_state)
        {
            if (!shader_program)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Material candidate requires a ShaderMap Program");
            }
            effective_state = shader_program->data().graphics_pass_state;
            if (two_sided)
            {
                effective_state.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
            }
            if (!shader::is_valid_shader_graphics_pass_state(effective_state))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Material candidate produced an invalid effective graphics Pass state");
            }
            return RHIStatus::success();
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
            const bool point = preset == MaterialSamplerPreset::PointClamp || preset == MaterialSamplerPreset::PointWrap;
            const bool trilinear = preset == MaterialSamplerPreset::TrilinearClamp ||
                preset == MaterialSamplerPreset::TrilinearWrap;
            const bool wrap = preset == MaterialSamplerPreset::PointWrap || preset == MaterialSamplerPreset::LinearWrap ||
                preset == MaterialSamplerPreset::TrilinearWrap;
            desc.min_filter = point ? RHIFilter::Nearest : RHIFilter::Linear;
            desc.mag_filter = desc.min_filter;
            desc.mip_filter = trilinear ? RHIFilter::Linear : RHIFilter::Nearest;
            desc.address_u = desc.address_v = desc.address_w = wrap ? RHIAddressMode::Repeat : RHIAddressMode::ClampToEdge;
            desc.debug_name = "MaterialSampler";
            return desc;
        }
    } // namespace

    MaterialRenderProxy::MaterialRenderProxy(const Material& material) : MaterialRenderProxy(material.desc()) {}

    MaterialRenderProxy::MaterialRenderProxy(const MaterialDesc& desc)
        : shader_name_(desc.shader_name), parameter_schema_(desc.parameter_schema),
          parameter_metadata_(make_material_parameter_metadata(parameter_schema_)),
          shader_program_(desc.shader_program)
    {
        if (shader_program_)
        {
            effective_graphics_pass_state_ = shader_program_->data().graphics_pass_state;
            if (desc.two_sided)
            {
                effective_graphics_pass_state_.cull_mode = shader::ShaderGraphicsPassState::CullMode::None;
            }
        }
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
        if (parameter_schema_.schema_identity == candidate.parameter_schema_.schema_identity &&
            scalar_parameters_ == candidate.scalar_parameters_ && vector2_parameters_ == candidate.vector2_parameters_ &&
            vector3_parameters_ == candidate.vector3_parameters_ && vector4_parameters_ == candidate.vector4_parameters_ &&
            texture_parameters_ == candidate.texture_parameters_ && sampler_parameters_ == candidate.sampler_parameters_)
        {
            candidate.binding_set_ = std::move(binding_set_);
            candidate.texture_generations_ = std::move(texture_generations_);
            candidate.texture_views_ = std::move(texture_views_);
            candidate.dirty_ = dirty_;
        }
        candidate.sampler_cache_ = std::move(sampler_cache_);
        candidate.resource_manager_ = resource_manager_;
        if (resource_manager_ != nullptr)
        {
            const auto status = candidate.begin_init_textures(*resource_manager_);
            if (!status) TOY_LOG_ERROR("Material candidate texture initialization failed: {}", status.message());
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
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    void MaterialRenderProxy::apply_vector_update(ShaderParameterId parameter_id, const vec2& value) noexcept
    {
        const auto current = vector2_parameters_.find(parameter_id);
        if (current != vector2_parameters_.end() && current->second.x == value.x && current->second.y == value.y)
        {
            return;
        }
        vector2_parameters_[parameter_id] = value;
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
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
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
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
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
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
        if (resource_manager_ != nullptr && texture_resource != nullptr)
        {
            const RHIStatus status = begin_init_texture_resource(*texture_resource, *resource_manager_);
            if (!status)
            {
                TOY_LOG_ERROR("MaterialRenderProxy could not initialize a TextureResource update: {}",
                              status.message());
            }
        }
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    RHIStatus MaterialRenderProxy::begin_init_textures(RenderResourceManager& manager)
    {
        resource_manager_ = &manager;
        for (const auto& texture_parameter : texture_parameters_)
        {
            TextureResource* const resource = texture_parameter.second;
            if (resource == nullptr)
            {
                return RHIStatus::failure(RHIErrorCode::NotReady, "Material texture parameter has no TextureResource");
            }
            const RHIStatus status = begin_init_texture_resource(*resource, manager);
            if (!status)
            {
                return status;
            }
        }
        return RHIStatus::success();
    }

    bool MaterialRenderProxy::texture_cache_matches(bool staged) const noexcept
    {
        const auto& generations = staged ? staged_texture_generations_ : texture_generations_;
        const auto& views = staged ? staged_texture_views_ : texture_views_;
        if (generations.size() != views.size())
        {
            return false;
        }
        for (const auto& generation : generations)
        {
            TextureResource* const resource = generation.first;
            const auto view = views.find(resource);
            if (resource == nullptr || view == views.end() || generation.second != resource->binding_generation() ||
                view->second != resource->view_for_current_recording())
            {
                return false;
            }
        }
        return true;
    }

    bool MaterialRenderProxy::texture_views_match(bool staged) const noexcept
    {
        const auto& views = staged ? staged_texture_views_ : texture_views_;
        const auto& generations = staged ? staged_texture_generations_ : texture_generations_;
        if (views.size() != generations.size())
        {
            return false;
        }
        for (const auto& view : views)
        {
            TextureResource* const resource = view.first;
            if (resource == nullptr || generations.count(resource) != 1u ||
                view.second != resource->view_for_current_recording())
            {
                return false;
            }
        }
        return true;
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize(RHIDevice& device, RHICommandContext& context)
    {
        return materialize_program(device, context, shader_program_, false);
    }

    RHIStatus MaterialRenderProxy::stage_material_candidate(ShaderMapProgramRef shader_program, bool two_sided)
    {
        if (!shader_program || staged_shader_program_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "MaterialRenderProxy requires exactly one complete staged ShaderMap Program");
        }
        if (shader_program->data().shader_name != shader_name_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Staged ShaderMap Program does not match the Material identity");
        }
        const shader::ShaderParameterSchema candidate_schema =
            material_parameter_schema_from_shader_schema(shader_program->data().parameter_schema);
        if (candidate_schema.schema_identity != parameter_schema_.schema_identity)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Staged ShaderMap Program has a different complete Material schema");
        }

        shader::ShaderGraphicsPassState effective_state;
        const RHIStatus state_status = derive_effective_graphics_pass_state(shader_program, two_sided, effective_state);
        if (!state_status)
        {
            return state_status;
        }

        staged_shader_program_ = std::move(shader_program);
        staged_effective_graphics_pass_state_ = effective_state;
        // A Program candidate does not participate in the Material logical cache key.
        // Copying the immutable active snapshot preserves publication isolation while
        // the normal value/view/generation checks still rebuild a genuinely stale candidate.
        staged_binding_set_ = binding_set_;
        staged_texture_generations_ = texture_generations_;
        staged_texture_views_ = texture_views_;
        staged_dirty_ = dirty_ || !staged_binding_set_;
        staged_materialized_ = false;
        return RHIStatus::success();
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_staged(RHIDevice& device, RHICommandContext& context)
    {
        RHIResult<RHIBindingSetRef> result = materialize_program(device, context, staged_shader_program_, true);
        staged_materialized_ = result.succeeded();
        return result;
    }

    RHIStatus MaterialRenderProxy::commit_material_candidate()
    {
        if (!staged_shader_program_ || !staged_materialized_ || !staged_binding_set_ || staged_dirty_ ||
            !texture_views_match(true))
        {
            const RHIStatus status = RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Material candidate binding must be current and fully materialized before submit commit");
            discard_material_candidate();
            return status;
        }

        for (auto& generation : staged_texture_generations_)
        {
            generation.second = generation.first->binding_generation();
        }

        shader_program_ = std::move(staged_shader_program_);
        effective_graphics_pass_state_ = staged_effective_graphics_pass_state_;
        binding_set_ = std::move(staged_binding_set_);
        texture_generations_ = std::move(staged_texture_generations_);
        texture_views_ = std::move(staged_texture_views_);
        dirty_ = false;
        staged_dirty_ = false;
        staged_materialized_ = false;
        return RHIStatus::success();
    }

    void MaterialRenderProxy::discard_material_candidate() noexcept
    {
        staged_shader_program_.reset();
        staged_effective_graphics_pass_state_ = {};
        staged_binding_set_.reset();
        staged_texture_generations_.clear();
        staged_texture_views_.clear();
        staged_dirty_ = false;
        staged_materialized_ = false;
    }

    const shader::ShaderGraphicsPassState* MaterialRenderProxy::effective_graphics_pass_state() const noexcept
    {
        return shader_program_ ? &effective_graphics_pass_state_ : nullptr;
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_program(RHIDevice& device, RHICommandContext& context,
                                                                         const ShaderMapProgramRef& shader_program,
                                                                         bool staged)
    {
        if (!shader_program)
        {
            return RHIResult<RHIBindingSetRef>::failure(
                RHIErrorCode::NotReady, "Material binding requires a ShaderMap Program");
        }

        RHIBindingSetRef& cached_set = staged ? staged_binding_set_ : binding_set_;
        bool& dirty = staged ? staged_dirty_ : dirty_;
        if (!dirty && cached_set && texture_cache_matches(staged))
        {
            return RHIResult<RHIBindingSetRef>::success(cached_set);
        }

        const RHIStatus metadata_status =
            validate_shader_parameters_metadata_against_schema(parameter_metadata_, parameter_schema_);
        if (!metadata_status)
            return RHIResult<RHIBindingSetRef>::failure(metadata_status.code(), metadata_status.message());

        ShaderParameterEncoder encoder(parameter_metadata_);
        for (const ShaderParameterConstantMemberMetadata& member : parameter_metadata_.constant_buffer.members)
        {
            const RHIStatus status = write_material_constant(member, scalar_parameters_, vector2_parameters_,
                                                             vector3_parameters_, vector4_parameters_, encoder);
            if (!status)
                return RHIResult<RHIBindingSetRef>::failure(status.code(), status.message());
        }

        std::unordered_map<TextureResource*, std::uint64_t> generations;
        std::unordered_map<TextureResource*, RHITextureViewRef> views;
        for (const ShaderParameterResourceMetadata& resource_metadata : parameter_metadata_.resources)
        {
            if (resource_metadata.category == shader::ShaderParameterCategory::Sampler)
            {
                const auto parameter = sampler_parameters_.find(resource_metadata.parameter_id);
                if (parameter == sampler_parameters_.end())
                    return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument,
                        "Material Sampler value is missing");
                if (parameter->second < MaterialSamplerPreset::PointClamp ||
                    parameter->second > MaterialSamplerPreset::TrilinearWrap)
                    return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::Unsupported,
                        "Material Sampler preset is not supported by this binding");
                auto cached = sampler_cache_.find(parameter->second);
                if (cached == sampler_cache_.end())
                {
                    auto created_sampler = device.create_sampler(material_sampler_desc(parameter->second));
                    if (!created_sampler)
                        return RHIResult<RHIBindingSetRef>::failure(created_sampler.status().code(),
                            created_sampler.status().message());
                    cached = sampler_cache_.emplace(parameter->second, created_sampler.value()).first;
                }
                encoder.add_resource(resource_metadata, cached->second);
                if (!encoder.succeeded())
                    return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument, encoder.error());
                continue;
            }
            const auto parameter = texture_parameters_.find(resource_metadata.parameter_id);
            TextureResource* const resource =
                parameter != texture_parameters_.end() ? parameter->second : nullptr;
            const RHITextureViewRef view = resource != nullptr ? resource->view_for_current_recording() : nullptr;
            if (resource == nullptr || !view)
            {
                return RHIResult<RHIBindingSetRef>::failure(
                    RHIErrorCode::NotReady, "Material TextureResource has no view for the current recording");
            }
            encoder.add_resource(resource_metadata, view);
            if (!encoder.succeeded())
                return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument, encoder.error());
            generations[resource] = resource->binding_generation();
            views[resource] = view;
        }

        RHIResult<RHIBindingSetRef> created = create_persistent_shader_binding(
            device, context, parameter_metadata_, encoder, shader_name_ + " MaterialBindings");
        if (!created)
        {
            return created;
        }
        cached_set = created.value();
        if (staged)
        {
            staged_texture_generations_ = std::move(generations);
            staged_texture_views_ = std::move(views);
        }
        else
        {
            texture_generations_ = std::move(generations);
            texture_views_ = std::move(views);
        }
        dirty = false;
        return RHIResult<RHIBindingSetRef>::success(cached_set);
    }
} // namespace toy3d
