#include "renderscene/postprocess/tonemap_pass.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "shader_parameters/toy3d_postprocess_tonemap.generated.h"

namespace toy3d
{
    const GlobalShaderType& tonemap_global_shader_type()
    {
        static const TonemapPassParameters parameters;
        const ShaderParametersMetadata& metadata = shader_parameters_metadata(parameters);
        static const GlobalShaderType type(
            "TonemapGlobalShader", "Toy3d/PostProcess/Tonemap", "Tonemap", shader::default_shader_permutation_key,
            GlobalShaderType::ProgramKind::Graphics, RHIShaderStageFlags::Vertex | RHIShaderStageFlags::Pixel,
            metadata,
            {GlobalShaderBindingRequirement(
                 metadata.constant_buffer.binding_id,
                 RHIBindingGroup::Pass, RHIResourceBindingType::UniformBuffer, 1, RHIShaderStageFlags::Pixel),
             GlobalShaderBindingRequirement(
                 metadata.resources[0u].parameter_id,
                 RHIBindingGroup::Pass, RHIResourceBindingType::SampledTexture, 1, RHIShaderStageFlags::Pixel),
             GlobalShaderBindingRequirement(
                 metadata.resources[1u].parameter_id,
                 RHIBindingGroup::Pass, RHIResourceBindingType::Sampler, 1, RHIShaderStageFlags::Pixel)});
        return type;
    }

    namespace
    {
        const GlobalShaderTypeRegistration tonemap_global_shader_registration(tonemap_global_shader_type());

    } // namespace

    float tonemap_sdr_channel_reference(float linear_value, float exposure_ev)
    {
        if (!std::isfinite(linear_value) || !std::isfinite(exposure_ev))
        {
            return 0.0F;
        }
        const float exposure = std::exp2(std::max(-32.0F, std::min(exposure_ev, 32.0F)));
        const float value = std::max(0.0F, std::min(linear_value * exposure, 65504.0F));
        const float numerator = value * (2.51F * value + 0.03F);
        const float denominator = value * (2.43F * value + 0.59F) + 0.14F;
        const float mapped = std::max(0.0F, std::min(numerator / denominator, 1.0F));
        return mapped <= 0.0031308F ? 12.92F * mapped : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
    }

    RHIStatus TonemapPassResources::initialize(RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
                                               const GlobalShaderMap& global_shader_map)
    {
        if (initialized())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "TonemapPassResources is already initialized.");
        }
        const ShaderMapProgramResult found = global_shader_map.find(tonemap_global_shader_type());
        if (!found.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Tonemap Global Shader lookup failed: " + found.error);
        }
        const ShaderMapProgramRef& shader_program = found.program;

        RHIResult<RHIShaderProgramRef> created_program = shader_program_cache.find_or_create(shader_program);
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
        RHIResult<RHISamplerRef> created_sampler = device.create_sampler(sampler_desc);
        if (!created_sampler)
        {
            return created_sampler.status();
        }

        RHIGraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = created_program.value()->vertex_shader;
        pipeline_desc.pixel_shader = created_program.value()->pixel_shader;
        pipeline_desc.binding_layout = created_program.value()->binding_layout;
        pipeline_desc.primitive_topology = RHIPrimitiveTopology::TriangleList;
        pipeline_desc.color_attachment_count = 1u;
        pipeline_desc.color_formats[0] = PixelFormat::B8G8R8A8UNorm;
        pipeline_desc.sample_count = 1u;
        pipeline_desc.debug_name = "TonemapPipeline";
        const auto translated = build_shader_graphics_pipeline_desc(pipeline_desc, shader_program->data().graphics_pass_state);
        if (!translated) return translated.status();
        const auto& depth = translated.value().depth_stencil;
        // Tonemap renders into a color-only output; reject incompatible source
        // state before a new Global Shader group can become active.
        if (depth.depth_test_enable || depth.depth_write_enable || depth.stencil_test_enable)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Tonemap Shader cannot enable depth or stencil without a depth attachment.");
        RHIResult<RHIGraphicsPipelineRef> created_pipeline = device.create_graphics_pipeline(translated.value());
        if (!created_pipeline)
        {
            return created_pipeline.status();
        }

        rhi_program_ = std::move(created_program).value();
        sampler_ = std::move(created_sampler).value();
        pipeline_ = std::move(created_pipeline).value();
        return RHIStatus::success();
    }

    void TonemapPassResources::release() noexcept
    {
        pipeline_.reset();
        sampler_.reset();
        rhi_program_.reset();
    }

    RHIStatus TonemapPassResources::render(RHIDevice& device, RHIGraphicsCommandContext& context,
                                           const RHITextureViewRef& scene_color, const TonemapPassTarget& target,
                                           const TonemapParameters& parameters) const
    {
        if (!initialized() || !target.color_view || target.extent.width == 0u ||
            target.extent.height == 0u ||
            target.format != PixelFormat::B8G8R8A8UNorm || target.sample_count != 1u ||
            target.color_view->desc().format != target.format ||
            target.color_view->texture()->desc().sample_count != target.sample_count)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Tonemap pass target must match the SDR B8G8R8A8UNorm single-sample output profile.");
        }
        if (!std::isfinite(parameters.exposure_ev))
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Tonemap exposure must be finite.");
        }

        TonemapPassParameters pass_parameters;
        pass_parameters.exposure_ev = parameters.exposure_ev;
        pass_parameters.scene_color = scene_color;
        pass_parameters.scene_sampler = sampler_;
        RHIResult<RHIBindingSetRef> binding_set =
            create_transient_shader_binding(device, context, pass_parameters);
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
        if (!status)
            return status;
        status = context.set_graphics_pipeline(pipeline_);
        if (!status)
            return status;
        // The fullscreen triangle is generated from SV_VertexID. Clear any
        // Base Pass vertex streams so the zero-layout pipeline does not inherit
        // incompatible dynamic bindings from the preceding pass.
        status = context.set_vertex_buffers({});
        if (!status)
            return status;
        RHIViewport viewport;
        viewport.width = static_cast<float>(target.extent.width);
        viewport.height = static_cast<float>(target.extent.height);
        status = context.set_viewport(viewport);
        if (!status)
            return status;
        RHIRect scissor;
        scissor.width = target.extent.width;
        scissor.height = target.extent.height;
        status = context.set_scissor(scissor);
        if (!status)
            return status;
        RHIGraphicsBindings bindings;
        bindings.pass = std::move(binding_set).value();
        status = context.bind_graphics_bindings(bindings);
        if (!status)
            return status;
        RHIDrawArgs draw;
        draw.vertex_count = 3u;
        status = context.draw(draw);
        if (!status)
            return status;
        return context.end_render_pass();
    }

    bool TonemapPassResources::initialized() const noexcept
    {
        return rhi_program_ && rhi_program_->vertex_shader && rhi_program_->pixel_shader &&
               rhi_program_->binding_layout && sampler_ && pipeline_;
    }
} // namespace toy3d
