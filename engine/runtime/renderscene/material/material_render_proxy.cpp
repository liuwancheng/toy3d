#include "renderscene/material/material_render_proxy.h"

#include "drivers/rhi/rhi_command_context.h"
#include "logging/logger.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/shader_uniform_buffer.h"
#include "renderscene/render_resource.h"
#include "renderscene/render_resource_manager.h"
#include "renderscene/texture/texture_resource.h"

#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        RHIStatus write_float_bytes(
            std::vector<std::uint8_t>& bytes,
            std::uint32_t offset,
            const float* values,
            std::uint32_t value_count,
            std::uint32_t reflected_size)
        {
            if (values == nullptr || value_count == 0)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Material constant source is empty");
            }
            const std::uint64_t byte_count =
                static_cast<std::uint64_t>(value_count) * sizeof(float);
            if (byte_count != reflected_size ||
                static_cast<std::uint64_t>(offset) + byte_count > bytes.size())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Material constant reflection range is invalid");
            }
            std::memcpy(bytes.data() + offset, values,
                static_cast<std::size_t>(byte_count));
            return RHIStatus::success();
        }

        RHIStatus write_constant(
            const ShaderMapBinding::ConstantMember& member,
            const std::unordered_map<ShaderParameterId, float>& scalars,
            const std::unordered_map<ShaderParameterId, vec2>& vectors2,
            const std::unordered_map<ShaderParameterId, vec3>& vectors3,
            const std::unordered_map<ShaderParameterId, vec4>& vectors4,
            std::vector<std::uint8_t>& bytes)
        {
            switch (member.type)
            {
            case ShaderValueType::Float32:
            {
                const auto found = scalars.find(member.parameter_id);
                return found == scalars.end()
                    ? RHIStatus::failure(RHIErrorCode::InvalidArgument,
                        "Material scalar value is missing")
                    : write_float_bytes(bytes, member.offset, &found->second,
                        1, member.size);
            }
            case ShaderValueType::Float32x2:
            {
                const auto found = vectors2.find(member.parameter_id);
                if (found == vectors2.end())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Material float2 value is missing");
                }
                const float values[] = {found->second.x, found->second.y};
                return write_float_bytes(
                    bytes, member.offset, values, 2, member.size);
            }
            case ShaderValueType::Float32x3:
            {
                const auto found = vectors3.find(member.parameter_id);
                if (found == vectors3.end())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Material float3 value is missing");
                }
                const float values[] = {
                    found->second.x, found->second.y, found->second.z};
                return write_float_bytes(
                    bytes, member.offset, values, 3, member.size);
            }
            case ShaderValueType::Float32x4:
            {
                const auto found = vectors4.find(member.parameter_id);
                if (found == vectors4.end())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Material float4 value is missing");
                }
                const float values[] = {found->second.x, found->second.y,
                    found->second.z, found->second.w};
                return write_float_bytes(
                    bytes, member.offset, values, 4, member.size);
            }
            default:
                return RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "Material constant type is not supported by the first-stage proxy");
            }
        }

        RHIStatus derive_effective_graphics_pass_state(
            const ShaderMapProgramRef& shader_program,
            bool two_sided,
            shader::ShaderGraphicsPassState& effective_state)
        {
            if (!shader_program)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Material candidate requires a ShaderMap Program");
            }
            effective_state = shader_program->data().graphics_pass_state;
            if (two_sided)
            {
                effective_state.cull_mode =
                    shader::ShaderGraphicsPassState::CullMode::None;
            }
            if (!shader::is_valid_shader_graphics_pass_state(effective_state))
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Material candidate produced an invalid effective graphics Pass state");
            }
            return RHIStatus::success();
        }

        RHIStatus begin_init_texture_resource(
            TextureResource& resource,
            RenderResourceManager& manager)
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
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Material texture resource was already released");
            }
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Material texture resource has an unknown state");
        }
    }

    MaterialRenderProxy::MaterialRenderProxy(const Material& material)
        : shader_name_(material.desc().shader_name),
          shader_program_(material.desc().shader_program)
    {
        if (shader_program_)
        {
            effective_graphics_pass_state_ =
                shader_program_->data().graphics_pass_state;
            if (material.desc().two_sided)
            {
                effective_graphics_pass_state_.cull_mode =
                    shader::ShaderGraphicsPassState::CullMode::None;
            }
        }
        scalar_parameters_ = material.desc().scalar_defaults;
        vector2_parameters_ = material.desc().vector2_defaults;
        vector3_parameters_ = material.desc().vector3_defaults;
        vector4_parameters_ = material.desc().vector4_defaults;
        for (const auto& default_texture : material.desc().texture_defaults)
        {
            texture_parameters_[default_texture.first] =
                default_texture.second
                    ? default_texture.second->texture_resource()
                    : nullptr;
        }
    }

    void MaterialRenderProxy::set_scalar(
        ShaderParameterId parameter_id,
        float value) noexcept
    {
        scalar_parameters_[parameter_id] = value;
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    void MaterialRenderProxy::set_vector(
        ShaderParameterId parameter_id,
        const vec2& value) noexcept
    {
        vector2_parameters_[parameter_id] = value;
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    void MaterialRenderProxy::set_vector(
        ShaderParameterId parameter_id,
        const vec3& value) noexcept
    {
        vector3_parameters_[parameter_id] = value;
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    void MaterialRenderProxy::set_vector(
        ShaderParameterId parameter_id,
        const vec4& value) noexcept
    {
        vector4_parameters_[parameter_id] = value;
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    void MaterialRenderProxy::set_texture(
        ShaderParameterId parameter_id,
        TextureResource* texture_resource) noexcept
    {
        texture_parameters_[parameter_id] = texture_resource;
        if (resource_manager_ != nullptr && texture_resource != nullptr)
        {
            const RHIStatus status = begin_init_texture_resource(
                *texture_resource, *resource_manager_);
            if (!status)
            {
                TOY_LOG_ERROR(
                    "MaterialRenderProxy could not initialize a TextureResource update: {}",
                    status.message());
            }
        }
        dirty_ = true;
        staged_dirty_ = staged_shader_program_ != nullptr;
    }

    RHIStatus MaterialRenderProxy::begin_init_textures(
        RenderResourceManager& manager)
    {
        resource_manager_ = &manager;
        for (const auto& texture_parameter : texture_parameters_)
        {
            TextureResource* const resource = texture_parameter.second;
            if (resource == nullptr)
            {
                return RHIStatus::failure(
                    RHIErrorCode::NotReady,
                    "Material texture parameter has no TextureResource");
            }
            const RHIStatus status = begin_init_texture_resource(
                *resource, manager);
            if (!status)
            {
                return status;
            }
        }
        return RHIStatus::success();
    }

    bool MaterialRenderProxy::texture_cache_matches(bool staged) const noexcept
    {
        const auto& generations = staged
            ? staged_texture_generations_
            : texture_generations_;
        const auto& views = staged ? staged_texture_views_ : texture_views_;
        if (generations.size() != views.size())
        {
            return false;
        }
        for (const auto& generation : generations)
        {
            TextureResource* const resource = generation.first;
            const auto view = views.find(resource);
            if (resource == nullptr || view == views.end() ||
                generation.second != resource->binding_generation() ||
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
        const auto& generations = staged
            ? staged_texture_generations_
            : texture_generations_;
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

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize(
        RHIDevice& device,
        RHICommandContext& context,
        const RHIBindingLayoutRef& binding_layout)
    {
        return materialize_program(
            device, context, binding_layout, shader_program_, false);
    }

    RHIStatus MaterialRenderProxy::stage_material_candidate(
        ShaderMapProgramRef shader_program,
        bool two_sided)
    {
        if (!shader_program || staged_shader_program_)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "MaterialRenderProxy requires exactly one complete staged ShaderMap Program");
        }
        if (shader_program->data().shader_name != shader_name_)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Staged ShaderMap Program does not match the Material identity");
        }

        shader::ShaderGraphicsPassState effective_state;
        const RHIStatus state_status = derive_effective_graphics_pass_state(
            shader_program, two_sided, effective_state);
        if (!state_status)
        {
            return state_status;
        }

        staged_shader_program_ = std::move(shader_program);
        staged_effective_graphics_pass_state_ = effective_state;
        staged_binding_layout_.reset();
        staged_binding_set_.reset();
        staged_texture_generations_.clear();
        staged_texture_views_.clear();
        staged_dirty_ = true;
        return RHIStatus::success();
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_staged(
        RHIDevice& device,
        RHICommandContext& context,
        const RHIBindingLayoutRef& binding_layout)
    {
        return materialize_program(
            device, context, binding_layout, staged_shader_program_, true);
    }

    RHIStatus MaterialRenderProxy::commit_material_candidate()
    {
        if (!staged_shader_program_ || !staged_binding_set_ ||
            !staged_binding_layout_ || staged_dirty_ ||
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
        binding_layout_ = std::move(staged_binding_layout_);
        binding_set_ = std::move(staged_binding_set_);
        texture_generations_ = std::move(staged_texture_generations_);
        texture_views_ = std::move(staged_texture_views_);
        dirty_ = false;
        staged_dirty_ = false;
        return RHIStatus::success();
    }

    void MaterialRenderProxy::discard_material_candidate() noexcept
    {
        staged_shader_program_.reset();
        staged_effective_graphics_pass_state_ = {};
        staged_binding_layout_.reset();
        staged_binding_set_.reset();
        staged_texture_generations_.clear();
        staged_texture_views_.clear();
        staged_dirty_ = false;
    }

    const shader::ShaderGraphicsPassState*
    MaterialRenderProxy::effective_graphics_pass_state() const noexcept
    {
        return shader_program_ ? &effective_graphics_pass_state_ : nullptr;
    }

    RHIResult<RHIBindingSetRef> MaterialRenderProxy::materialize_program(
        RHIDevice& device,
        RHICommandContext& context,
        const RHIBindingLayoutRef& binding_layout,
        const ShaderMapProgramRef& shader_program,
        bool staged)
    {
        if (!shader_program || !binding_layout)
        {
            return RHIResult<RHIBindingSetRef>::failure(
                RHIErrorCode::NotReady,
                "Material binding requires a ShaderMap Program and RHI binding layout");
        }

        RHIBindingSetRef& cached_set = staged ? staged_binding_set_ : binding_set_;
        RHIBindingLayoutRef& cached_layout = staged
            ? staged_binding_layout_
            : binding_layout_;
        bool& dirty = staged ? staged_dirty_ : dirty_;
        if (!dirty && cached_set && cached_layout == binding_layout &&
            texture_cache_matches(staged))
        {
            return RHIResult<RHIBindingSetRef>::success(cached_set);
        }

        RHIBindingSetDesc desc;
        desc.layout = binding_layout;
        desc.group = RHIBindingGroup::Material;
        desc.debug_name = shader_program->data().shader_name +
            "/" + shader_program->data().pass_name + " MaterialBindings";

        std::unordered_map<TextureResource*, std::uint64_t> generations;
        std::unordered_map<TextureResource*, RHITextureViewRef> views;
        for (const ShaderMapBinding& binding : shader_program->data().bindings)
        {
            if (binding.group != RHIBindingGroup::Material)
            {
                continue;
            }

            if (binding.type == RHIResourceBindingType::UniformBuffer)
            {
                if (binding.constant_buffer_size == 0 ||
                    binding.constant_buffer_size >
                        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Material constant-buffer size is invalid");
                }

                std::vector<std::uint8_t> bytes(
                    binding.constant_buffer_size, 0);
                for (const ShaderMapBinding::ConstantMember& member :
                     binding.constant_members)
                {
                    const RHIStatus status = write_constant(
                        member, scalar_parameters_, vector2_parameters_,
                        vector3_parameters_, vector4_parameters_, bytes);
                    if (!status)
                    {
                        return RHIResult<RHIBindingSetRef>::failure(
                            status.code(), status.message());
                    }
                }

                RHIResult<RHIBufferRef> buffer =
                    create_uploaded_shader_uniform_buffer(
                        device, context, bytes,
                        desc.debug_name + " Constants");
                if (!buffer)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        buffer.status().code(), buffer.status().message());
                }

                RHIBindingValue value;
                value.slot = binding.target_binding;
                value.buffer = std::move(buffer).value();
                value.buffer_size = binding.constant_buffer_size;
                desc.bindings.push_back(std::move(value));
                continue;
            }

            if (binding.type == RHIResourceBindingType::SampledTexture &&
                binding.array_count == 1)
            {
                const auto parameter = texture_parameters_.find(
                    binding.parameter_id);
                TextureResource* const resource =
                    parameter == texture_parameters_.end()
                        ? nullptr
                        : parameter->second;
                const RHITextureViewRef view = resource != nullptr
                    ? resource->view_for_current_recording()
                    : nullptr;
                if (resource == nullptr || !view)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::NotReady,
                        "Material TextureResource has no view for the current recording");
                }

                RHIBindingValue value;
                value.slot = binding.target_binding;
                value.texture_view = view;
                desc.bindings.push_back(std::move(value));
                generations[resource] = resource->binding_generation();
                views[resource] = view;
                continue;
            }

            return RHIResult<RHIBindingSetRef>::failure(
                RHIErrorCode::Unsupported,
                "Material binding requires an unsupported first-stage resource type");
        }

        RHIResult<RHIBindingSetRef> created = device.create_binding_set(desc);
        if (!created)
        {
            return created;
        }
        cached_layout = binding_layout;
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
}
