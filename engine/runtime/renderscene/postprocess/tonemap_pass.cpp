#include "renderscene/postprocess/tonemap_pass.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_uniform_buffer.h"

namespace toy3d
{
    namespace
    {
        const ShaderMapBinding* find_binding(
            const ShaderMapProgram& program,
            const char* name,
            RHIResourceBindingType type)
        {
            for (const ShaderMapBinding& binding : program.data().bindings)
            {
                if (binding.group == RHIBindingGroup::Pass &&
                    binding.type == type && binding.name == name)
                {
                    return &binding;
                }
            }
            return nullptr;
        }

        const ShaderMapBinding::ConstantMember* find_constant_member(
            const ShaderMapBinding& binding,
            const char* name)
        {
            for (const ShaderMapBinding::ConstantMember& member :
                 binding.constant_members)
            {
                if (member.name == name)
                {
                    return &member;
                }
            }
            return nullptr;
        }
    }

    float tonemap_sdr_channel_reference(float linear_value, float exposure_ev)
    {
        if (!std::isfinite(linear_value) || !std::isfinite(exposure_ev))
        {
            return 0.0F;
        }
        const float exposure = std::exp2(
            std::max(-32.0F, std::min(exposure_ev, 32.0F)));
        const float value = std::max(
            0.0F, std::min(linear_value * exposure, 65504.0F));
        const float numerator = value * (2.51F * value + 0.03F);
        const float denominator = value * (2.43F * value + 0.59F) + 0.14F;
        const float mapped = std::max(
            0.0F, std::min(numerator / denominator, 1.0F));
        return mapped <= 0.0031308F
            ? 12.92F * mapped
            : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
    }

    RHIStatus TonemapPassResources::initialize(
        RHIDevice& device,
        const ShaderMapProgram& shader_program)
    {
        if (initialized())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "TonemapPassResources is already initialized.");
        }
        const ShaderMapProgramData& data = shader_program.data();
        if (data.shader_name != "Toy3d/PostProcess/Tonemap" ||
            data.pass_name != "Tonemap" ||
            data.platform != ShaderPlatform::VulkanPortableV1)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Tonemap ShaderMap Program identity or platform is invalid.");
        }

        const ShaderMapBinding* constant_buffer = find_binding(
            shader_program, "toy_pass_data", RHIResourceBindingType::UniformBuffer);
        const ShaderMapBinding* scene_color = find_binding(
            shader_program, "scene_color", RHIResourceBindingType::SampledTexture);
        const ShaderMapBinding* scene_sampler = find_binding(
            shader_program, "scene_sampler", RHIResourceBindingType::Sampler);
        const ShaderMapBinding::ConstantMember* exposure =
            constant_buffer != nullptr
            ? find_constant_member(*constant_buffer, "exposure_ev")
            : nullptr;
        if (constant_buffer == nullptr || scene_color == nullptr ||
            scene_sampler == nullptr || exposure == nullptr ||
            exposure->type != ShaderValueType::Float32 ||
            exposure->size != sizeof(float) ||
            exposure->offset + exposure->size >
                constant_buffer->constant_buffer_size)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Tonemap ShaderMap Program does not match the required Pass binding schema.");
        }

        RHIResult<RHIShaderProgram> created_program =
            create_rhi_shader_program(device, shader_program);
        if (!created_program)
        {
            return created_program.status();
        }

        RHISamplerDesc sampler_desc;
        sampler_desc.min_filter = RHIFilter::Linear;
        sampler_desc.mag_filter = RHIFilter::Linear;
        sampler_desc.mip_filter = RHIFilter::Linear;
        sampler_desc.address_u = RHIAddressMode::ClampToEdge;
        sampler_desc.address_v = RHIAddressMode::ClampToEdge;
        sampler_desc.address_w = RHIAddressMode::ClampToEdge;
        sampler_desc.debug_name = "TonemapSceneSampler";
        RHIResult<RHISamplerRef> created_sampler =
            device.create_sampler(sampler_desc);
        if (!created_sampler)
        {
            return created_sampler.status();
        }

        RHIGraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = created_program.value().vertex_shader;
        pipeline_desc.pixel_shader = created_program.value().pixel_shader;
        pipeline_desc.binding_layout = created_program.value().binding_layout;
        pipeline_desc.primitive_topology = RHIPrimitiveTopology::TriangleList;
        pipeline_desc.rasterization.cull_mode = RHICullMode::None;
        pipeline_desc.depth_stencil.depth_test_enable = false;
        pipeline_desc.depth_stencil.depth_write_enable = false;
        pipeline_desc.color_attachment_count = 1u;
        pipeline_desc.color_formats[0] = PixelFormat::B8G8R8A8UNorm;
        pipeline_desc.sample_count = 1u;
        pipeline_desc.debug_name = "TonemapPipeline";
        RHIResult<RHIGraphicsPipelineRef> created_pipeline =
            device.create_graphics_pipeline(pipeline_desc);
        if (!created_pipeline)
        {
            return created_pipeline.status();
        }

        shader_program_ = &shader_program;
        constant_buffer_binding_ = constant_buffer;
        exposure_binding_ = exposure;
        scene_color_binding_ = scene_color;
        scene_sampler_binding_ = scene_sampler;
        rhi_program_ = std::move(created_program).value();
        sampler_ = std::move(created_sampler).value();
        pipeline_ = std::move(created_pipeline).value();
        return RHIStatus::success();
    }

    void TonemapPassResources::release() noexcept
    {
        pipeline_.reset();
        sampler_.reset();
        rhi_program_ = {};
        scene_sampler_binding_ = nullptr;
        scene_color_binding_ = nullptr;
        exposure_binding_ = nullptr;
        constant_buffer_binding_ = nullptr;
        shader_program_ = nullptr;
    }

    RHIStatus TonemapPassResources::render(
        RHIDevice& device,
        RHIGraphicsCommandContext& context,
        const RHITextureViewRef& scene_color,
        const TonemapPassTarget& target,
        const TonemapParameters& parameters) const
    {
        if (!initialized() || !scene_color || !target.color_view ||
            target.width == 0u || target.height == 0u ||
            target.format != PixelFormat::B8G8R8A8UNorm ||
            target.sample_count != 1u ||
            target.color_view->desc().format != target.format ||
            target.color_view->texture()->desc().sample_count !=
                target.sample_count)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Tonemap pass target must match the SDR B8G8R8A8UNorm single-sample output profile.");
        }
        if (!std::isfinite(parameters.exposure_ev))
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Tonemap exposure must be finite.");
        }

        std::vector<std::uint8_t> constants(
            constant_buffer_binding_->constant_buffer_size, 0u);
        std::memcpy(
            constants.data() + exposure_binding_->offset,
            &parameters.exposure_ev,
            sizeof(parameters.exposure_ev));
        RHIResult<RHIBufferRef> uniform_buffer =
            create_uploaded_shader_uniform_buffer(
                device, context, constants, "TonemapPassConstants");
        if (!uniform_buffer)
        {
            return uniform_buffer.status();
        }

        RHIBindingSetDesc binding_desc;
        binding_desc.layout = rhi_program_.binding_layout;
        binding_desc.group = RHIBindingGroup::Pass;
        binding_desc.debug_name = "TonemapPassBindings";
        RHIBindingValue constants_value;
        constants_value.slot = constant_buffer_binding_->target_binding;
        constants_value.buffer = std::move(uniform_buffer).value();
        constants_value.buffer_size =
            constant_buffer_binding_->constant_buffer_size;
        binding_desc.bindings.push_back(std::move(constants_value));
        RHIBindingValue texture_value;
        texture_value.slot = scene_color_binding_->target_binding;
        texture_value.texture_view = scene_color;
        binding_desc.bindings.push_back(std::move(texture_value));
        RHIBindingValue sampler_value;
        sampler_value.slot = scene_sampler_binding_->target_binding;
        sampler_value.sampler = sampler_;
        binding_desc.bindings.push_back(std::move(sampler_value));
        RHIResult<RHIBindingSetRef> binding_set =
            device.create_binding_set(binding_desc);
        if (!binding_set)
        {
            return binding_set.status();
        }

        RHIRenderPassDesc pass_desc;
        RHIColorAttachmentDesc color_attachment;
        color_attachment.view = target.color_view;
        color_attachment.load = RHILoadOperation::Discard;
        color_attachment.store = RHIStoreOperation::Store;
        pass_desc.color_attachments.push_back(std::move(color_attachment));
        pass_desc.debug_name = "TonemapPass";
        RHIStatus status = context.begin_render_pass(pass_desc);
        if (!status) return status;
        status = context.set_graphics_pipeline(pipeline_);
        if (!status) return status;
        // The fullscreen triangle is generated from SV_VertexID. Clear any
        // Base Pass vertex streams so the zero-layout pipeline does not inherit
        // incompatible dynamic bindings from the preceding pass.
        status = context.set_vertex_buffers({});
        if (!status) return status;
        RHIViewport viewport;
        viewport.width = static_cast<float>(target.width);
        viewport.height = static_cast<float>(target.height);
        status = context.set_viewport(viewport);
        if (!status) return status;
        RHIRect scissor;
        scissor.width = target.width;
        scissor.height = target.height;
        status = context.set_scissor(scissor);
        if (!status) return status;
        RHIGraphicsBindings bindings;
        bindings.pass = std::move(binding_set).value();
        status = context.bind_graphics_bindings(bindings);
        if (!status) return status;
        RHIDrawArgs draw;
        draw.vertex_count = 3u;
        status = context.draw(draw);
        if (!status) return status;
        return context.end_render_pass();
    }

    bool TonemapPassResources::initialized() const noexcept
    {
        return shader_program_ != nullptr && rhi_program_.vertex_shader &&
            rhi_program_.pixel_shader && rhi_program_.binding_layout &&
            sampler_ && pipeline_;
    }
}
