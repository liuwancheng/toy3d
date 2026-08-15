#include "renderscene/3dscene/scene_render.h"

#include "core/file_system/file_system.h"

#include <array>
#include <cstdint>
#include <exception>
#include <utility>

namespace toy3d
{
    namespace
    {
        std::array<std::uint64_t, 2> hash_shader_bytecode(const std::vector<std::uint8_t>& bytes)
        {
            std::uint64_t first = 1469598103934665603ULL;
            std::uint64_t second = 1099511628211ULL;
            for (const std::uint8_t byte : bytes)
            {
                first = (first ^ byte) * 1099511628211ULL;
                second = (second + byte) * 1469598103934665603ULL;
            }
            return {first, second};
        }

        RHIResult<RHIShaderRef> create_test_shader(
            RHIDevice& device,
            const char* path,
            RHIShaderStage stage,
            std::vector<RHIShaderBindingReflection> reflection)
        {
            try
            {
                RHIShaderDesc desc;
                desc.stage = stage;
                desc.bytecode.bytes = FileSystem::get_instance().read_file(path);
                desc.bytecode.target = "spirv";
                desc.entry_point = "main";
                desc.reflection = std::move(reflection);
                desc.content_hash = hash_shader_bytecode(desc.bytecode.bytes);
                desc.debug_name = path;
                return device.create_shader(desc);
            }
            catch (const std::exception& exception)
            {
                return RHIResult<RHIShaderRef>::failure(RHIErrorCode::BackendFailure, exception.what());
            }
        }
    }

    RHIStatus SceneRendering::initialize_test_pass_resources()
    {
        if (test_pipeline)
        {
            return RHIStatus::success();
        }

        RHIShaderBindingReflection texture_reflection;
        texture_reflection.name = "source_texture";
        texture_reflection.group = RHIBindingGroup::Material;
        texture_reflection.slot = 0;
        texture_reflection.type = RHIResourceBindingType::SampledTexture;
        RHIShaderBindingReflection sampler_reflection;
        sampler_reflection.name = "source_sampler";
        sampler_reflection.group = RHIBindingGroup::Material;
        sampler_reflection.slot = 0;
        sampler_reflection.type = RHIResourceBindingType::Sampler;

        auto vertex_shader_result = create_test_shader(
            rhi_device, "shader/test_pass.vert.spv", RHIShaderStage::Vertex, {});
        if (!vertex_shader_result)
        {
            return vertex_shader_result.status();
        }
        auto pixel_shader_result = create_test_shader(
            rhi_device,
            "shader/test_pass.frag.spv",
            RHIShaderStage::Pixel,
            {texture_reflection, sampler_reflection});
        if (!pixel_shader_result)
        {
            return pixel_shader_result.status();
        }

        RHIBindingLayoutDesc layout_desc;
        layout_desc.entries = {
            {RHIBindingGroup::Material, 0, RHIResourceBindingType::SampledTexture, RHIShaderStageFlags::Pixel, 1},
            {RHIBindingGroup::Material, 0, RHIResourceBindingType::Sampler, RHIShaderStageFlags::Pixel, 1}};
        layout_desc.debug_name = "TestPassBindingLayout";
        auto layout_result = rhi_device.create_binding_layout(layout_desc);
        if (!layout_result)
        {
            return layout_result.status();
        }
        RHIBindingLayoutRef binding_layout = std::move(layout_result).value();

        RHITextureDesc texture_desc;
        texture_desc.width = 4;
        texture_desc.height = 4;
        texture_desc.format = RHIFormat::R8G8B8A8UNorm;
        texture_desc.usage = rhi_enum_or(RHIResourceUsage::ShaderResource, RHIResourceUsage::CopyDestination);
        texture_desc.initial_access = RHIAccess::Common;
        texture_desc.debug_name = "TestPassCheckerboardTexture";
        auto texture_result = rhi_device.create_texture(texture_desc);
        if (!texture_result)
        {
            return texture_result.status();
        }
        RHITextureRef texture = std::move(texture_result).value();

        RHITextureViewDesc view_desc;
        view_desc.type = RHIResourceViewType::ShaderResource;
        view_desc.dimension = RHITextureViewDimension::Texture2D;
        view_desc.format = RHIFormat::R8G8B8A8UNorm;
        view_desc.debug_name = "TestPassCheckerboardView";
        auto view_result = rhi_device.create_texture_view(texture, view_desc);
        if (!view_result)
        {
            return view_result.status();
        }
        RHITextureViewRef texture_view = std::move(view_result).value();

        RHISamplerDesc sampler_desc;
        sampler_desc.min_filter = RHIFilter::Nearest;
        sampler_desc.mag_filter = RHIFilter::Nearest;
        sampler_desc.mip_filter = RHIFilter::Nearest;
        sampler_desc.address_u = RHIAddressMode::Repeat;
        sampler_desc.address_v = RHIAddressMode::Repeat;
        sampler_desc.debug_name = "TestPassCheckerboardSampler";
        auto sampler_result = rhi_device.create_sampler(sampler_desc);
        if (!sampler_result)
        {
            return sampler_result.status();
        }
        RHISamplerRef sampler = std::move(sampler_result).value();

        RHIBindingSetDesc binding_set_desc;
        binding_set_desc.layout = binding_layout;
        binding_set_desc.group = RHIBindingGroup::Material;
        RHIBindingValue texture_binding;
        texture_binding.slot = 0;
        texture_binding.texture_view = texture_view;
        RHIBindingValue sampler_binding;
        sampler_binding.slot = 0;
        sampler_binding.sampler = sampler;
        binding_set_desc.bindings = {texture_binding, sampler_binding};
        binding_set_desc.debug_name = "TestPassBindingSet";
        auto binding_set_result = rhi_device.create_binding_set(binding_set_desc);
        if (!binding_set_result)
        {
            return binding_set_result.status();
        }

        RHIGraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = std::move(vertex_shader_result).value();
        pipeline_desc.pixel_shader = std::move(pixel_shader_result).value();
        pipeline_desc.binding_layout = binding_layout;
        pipeline_desc.primitive_topology = RHIPrimitiveTopology::TriangleList;
        pipeline_desc.rasterization.cull_mode = RHICullMode::None;
        pipeline_desc.color_formats[0] = RHIFormat::B8G8R8A8UNorm;
        pipeline_desc.color_attachment_count = 1;
        pipeline_desc.depth_stencil.depth_test_enable = true;
        pipeline_desc.depth_stencil.depth_write_enable = true;
        pipeline_desc.depth_stencil.depth_compare_operation = RHICompareOperation::LessEqual;
        pipeline_desc.depth_stencil_format = RHIFormat::D32Float;
        pipeline_desc.debug_name = "TestPassFullscreenPipeline";
        auto pipeline_result = rhi_device.create_graphics_pipeline(pipeline_desc);
        if (!pipeline_result)
        {
            return pipeline_result.status();
        }

        test_texture = std::move(texture);
        test_texture_view = std::move(texture_view);
        test_sampler = std::move(sampler);
        test_binding_layout = std::move(binding_layout);
        test_binding_set = std::move(binding_set_result).value();
        test_pipeline = std::move(pipeline_result).value();
        return RHIStatus::success();
    }

    RHIStatus SceneRendering::initialize_test_depth_resources(const RHIFrameContext& frame)
    {
        if (test_depth_texture && test_depth_width == frame.width() &&
            test_depth_height == frame.height())
        {
            return RHIStatus::success();
        }

        RHITextureDesc texture_desc;
        texture_desc.width = frame.width();
        texture_desc.height = frame.height();
        texture_desc.format = RHIFormat::D32Float;
        texture_desc.usage = RHIResourceUsage::DepthStencil;
        texture_desc.initial_access = RHIAccess::Common;
        texture_desc.clear_value = RHIClearValue::DepthOne;
        texture_desc.debug_name = "TestPassDepth";
        auto texture_result = rhi_device.create_texture(texture_desc);
        if (!texture_result)
        {
            return texture_result.status();
        }

        RHITextureViewDesc view_desc;
        view_desc.type = RHIResourceViewType::DepthStencil;
        view_desc.dimension = RHITextureViewDimension::Texture2D;
        view_desc.format = RHIFormat::D32Float;
        view_desc.subresources.aspect = RHITextureAspect::Depth;
        view_desc.subresources.mip_count = 1;
        view_desc.subresources.layer_count = 1;
        view_desc.debug_name = "TestPassDepthView";
        auto view_result = rhi_device.create_texture_view(texture_result.value(), view_desc);
        if (!view_result)
        {
            return view_result.status();
        }

        test_depth_texture = std::move(texture_result).value();
        test_depth_view = std::move(view_result).value();
        test_depth_width = frame.width();
        test_depth_height = frame.height();
        test_depth_transitioned = false;
        return RHIStatus::success();
    }

    RHIStatus SceneRendering::prepare_test_pass_texture(RHIGraphicsCommandContext& context)
    {
        if (test_texture_uploaded)
        {
            return RHIStatus::success();
        }

        RHIResourceTransition to_copy;
        to_copy.resource = test_texture;
        to_copy.before = RHIAccess::Common;
        to_copy.after = RHIAccess::CopyDestination;
        RHIStatus status = context.transition_resources({to_copy});
        if (!status)
        {
            return status;
        }

        std::array<std::uint8_t, 4 * 4 * 4> pixels{};
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                const bool light = ((x + y) & 1U) == 0;
                const std::size_t offset = (y * 4 + x) * 4;
                pixels[offset + 0] = light ? 255 : 24;
                pixels[offset + 1] = light ? 72 : 32;
                pixels[offset + 2] = light ? 32 : 220;
                pixels[offset + 3] = 255;
            }
        }
        RHITextureUploadDesc upload_desc;
        upload_desc.destination.texture = test_texture;
        upload_desc.extent = {4, 4, 1};
        upload_desc.source.data = pixels.data();
        upload_desc.source.size = pixels.size();
        upload_desc.source.row_pitch = 4 * 4;
        upload_desc.source.slice_pitch = pixels.size();
        status = context.upload_texture(upload_desc);
        if (!status)
        {
            return status;
        }

        RHIResourceTransition to_shader_resource;
        to_shader_resource.resource = test_texture;
        to_shader_resource.before = RHIAccess::CopyDestination;
        to_shader_resource.after = RHIAccess::ShaderResourceGraphics;
        status = context.transition_resources({to_shader_resource});
        if (status)
        {
            test_texture_uploaded = true;
        }
        return status;
    }

    RHIStatus SceneRendering::prepare_test_pass_depth(RHIGraphicsCommandContext& context)
    {
        if (test_depth_transitioned)
        {
            return RHIStatus::success();
        }

        RHIResourceTransition transition;
        transition.resource = test_depth_texture;
        transition.before = RHIAccess::Common;
        transition.after = RHIAccess::DepthStencilWrite;
        transition.subresources.aspect = RHITextureAspect::Depth;
        transition.subresources.mip_count = 1;
        transition.subresources.layer_count = 1;
        const RHIStatus status = context.transition_resources({transition});
        if (status)
        {
            test_depth_transitioned = true;
        }
        return status;
    }

    RHIStatus SceneRendering::render_test_pass(
        RHIGraphicsCommandContext& context,
        const RHIFrameContext& frame)
    {
        RHIRenderPassDesc pass_desc;
        RHIColorAttachmentDesc color_attachment;
        color_attachment.view = frame.present_view();
        color_attachment.load = RHILoadOperation::Clear;
        color_attachment.store = RHIStoreOperation::Store;
        color_attachment.clear_value = RHIClearValue::color_value(vec4(0.04F, 0.08F, 0.16F, 1.0F));
        pass_desc.color_attachments.push_back(std::move(color_attachment));
        pass_desc.has_depth_stencil_attachment = true;
        pass_desc.depth_stencil_attachment.view = test_depth_view;
        pass_desc.depth_stencil_attachment.depth_load = RHILoadOperation::Clear;
        pass_desc.depth_stencil_attachment.depth_store = RHIStoreOperation::Store;
        pass_desc.depth_stencil_attachment.stencil_load = RHILoadOperation::Discard;
        pass_desc.depth_stencil_attachment.stencil_store = RHIStoreOperation::Discard;
        pass_desc.depth_stencil_attachment.clear_value = RHIClearValue::DepthOne;
        pass_desc.debug_name = "SceneRenderingTestPass";

        RHIStatus status = context.begin_render_pass(pass_desc);
        if (!status)
        {
            return status;
        }

        status = context.set_graphics_pipeline(test_pipeline);
        if (status)
        {
            RHIViewport viewport;
            viewport.width = static_cast<float>(frame.width());
            viewport.height = static_cast<float>(frame.height());
            status = context.set_viewport(viewport);
        }
        if (status)
        {
            RHIRect scissor;
            scissor.width = frame.width();
            scissor.height = frame.height();
            status = context.set_scissor(scissor);
        }
        if (status)
        {
            status = context.bind_binding_set(test_binding_set);
        }
        if (status)
        {
            RHIDrawArgs draw_args;
            draw_args.vertex_count = 3;
            status = context.draw(draw_args);
        }

        const RHIStatus end_status = context.end_render_pass();
        return status ? end_status : status;
    }
}
