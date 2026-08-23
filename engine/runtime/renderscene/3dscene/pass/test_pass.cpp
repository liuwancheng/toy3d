#include "renderscene/3dscene/scene_render.h"

#include "rendercore/shader/rhi_shader_program.h"
#include "rendercore/shader/shader_map.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace toy3d
{
    RHIStatus SceneRendering::initialize_test_pass_resources()
    {
        if (test_pipeline)
        {
            return RHIStatus::success();
        }

        ShaderMapProgramKey key;
        key.shader_name = "Toy3d/Test/TestPass";
        key.pass_name = "TestPass";
        ShaderMapProgramResult loaded = shader_map.find_or_load(key);
        if (!loaded.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::BackendFailure, loaded.error);
        }
        auto program_result = create_rhi_shader_program(rhi_device, *loaded.program);
        if (!program_result)
        {
            return program_result.status();
        }
        RHIShaderProgram program = std::move(program_result).value();
        if (!program.vertex_shader || !program.pixel_shader || program.compute_shader)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Test pass requires a vertex/pixel ShaderMap Program.");
        }
        const ShaderMapProgramData& shader_map_program = loaded.program->data();
        const auto find_binding = [&](const char* name, RHIBindingGroup group,
                                      RHIResourceBindingType type)
            -> const ShaderMapBinding* {
            const auto binding = std::find_if(
                shader_map_program.bindings.begin(), shader_map_program.bindings.end(),
                [&](const ShaderMapBinding& value) {
                    return value.name == name && value.group == group &&
                        value.type == type;
                });
            return binding == shader_map_program.bindings.end() ? nullptr : &*binding;
        };
        const ShaderMapBinding* texture_parameter = find_binding(
            "source_texture", RHIBindingGroup::Material,
            RHIResourceBindingType::SampledTexture);
        const ShaderMapBinding* sampler_parameter = find_binding(
            "source_sampler", RHIBindingGroup::Material,
            RHIResourceBindingType::Sampler);
        const ShaderMapBinding* global_texture_parameter = find_binding(
            "global_texture", RHIBindingGroup::Global,
            RHIResourceBindingType::SampledTexture);
        const ShaderMapBinding* view_texture_parameter = find_binding(
            "view_texture", RHIBindingGroup::View,
            RHIResourceBindingType::SampledTexture);
        if (!texture_parameter || !sampler_parameter ||
            !global_texture_parameter || !view_texture_parameter)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Test pass ShaderMap Program is missing Global, View, or Material bindings.");
        }
        RHIBindingLayoutRef binding_layout = program.binding_layout;

        RHITextureDesc texture_desc;
        texture_desc.width = 4;
        texture_desc.height = 4;
        texture_desc.format = PixelFormat::R8G8B8A8UNorm;
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
        view_desc.format = PixelFormat::R8G8B8A8UNorm;
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

        RHIBindingSetDesc material_binding_set_desc;
        material_binding_set_desc.layout = binding_layout;
        material_binding_set_desc.group = RHIBindingGroup::Material;
        RHIBindingValue texture_binding;
        texture_binding.slot = texture_parameter->target_binding;
        texture_binding.texture_view = texture_view;
        RHIBindingValue sampler_binding;
        sampler_binding.slot = sampler_parameter->target_binding;
        sampler_binding.sampler = sampler;
        material_binding_set_desc.bindings = {texture_binding, sampler_binding};
        material_binding_set_desc.debug_name = "TestPassMaterialBindingSet";
        auto material_binding_set_result = rhi_device.create_binding_set(
            material_binding_set_desc);
        if (!material_binding_set_result)
        {
            return material_binding_set_result.status();
        }

        RHIBindingSetDesc global_binding_set_desc;
        global_binding_set_desc.layout = binding_layout;
        global_binding_set_desc.group = RHIBindingGroup::Global;
        RHIBindingValue global_texture_binding;
        global_texture_binding.slot = global_texture_parameter->target_binding;
        global_texture_binding.texture_view = texture_view;
        global_binding_set_desc.bindings = {global_texture_binding};
        global_binding_set_desc.debug_name = "TestPassGlobalBindingSet";
        auto global_binding_set_result = rhi_device.create_binding_set(
            global_binding_set_desc);
        if (!global_binding_set_result)
        {
            return global_binding_set_result.status();
        }

        RHIBindingSetDesc view_binding_set_desc;
        view_binding_set_desc.layout = binding_layout;
        view_binding_set_desc.group = RHIBindingGroup::View;
        RHIBindingValue view_texture_binding;
        view_texture_binding.slot = view_texture_parameter->target_binding;
        view_texture_binding.texture_view = texture_view;
        view_binding_set_desc.bindings = {view_texture_binding};
        view_binding_set_desc.debug_name = "TestPassViewBindingSet";
        auto view_binding_set_result = rhi_device.create_binding_set(
            view_binding_set_desc);
        if (!view_binding_set_result)
        {
            return view_binding_set_result.status();
        }

        RHIGraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = std::move(program.vertex_shader);
        pipeline_desc.pixel_shader = std::move(program.pixel_shader);
        pipeline_desc.binding_layout = binding_layout;
        pipeline_desc.primitive_topology = RHIPrimitiveTopology::TriangleList;
        pipeline_desc.rasterization.cull_mode = RHICullMode::None;
        pipeline_desc.color_formats[0] = PixelFormat::B8G8R8A8UNorm;
        pipeline_desc.color_attachment_count = 1;
        pipeline_desc.depth_stencil.depth_test_enable = true;
        pipeline_desc.depth_stencil.depth_write_enable = true;
        pipeline_desc.depth_stencil.depth_compare_operation = RHICompareOperation::GreaterEqual;
        pipeline_desc.depth_stencil_format = PixelFormat::D32Float;
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
        test_global_binding_set = std::move(global_binding_set_result).value();
        test_view_binding_set = std::move(view_binding_set_result).value();
        test_material_binding_set = std::move(material_binding_set_result).value();
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
        texture_desc.format = PixelFormat::D32Float;
        texture_desc.usage = RHIResourceUsage::DepthStencil;
        texture_desc.initial_access = RHIAccess::Common;
        texture_desc.clear_value = RHIClearValue::DepthZero;
        texture_desc.debug_name = "TestPassDepth";
        auto texture_result = rhi_device.create_texture(texture_desc);
        if (!texture_result)
        {
            return texture_result.status();
        }

        RHITextureViewDesc view_desc;
        view_desc.type = RHIResourceViewType::DepthStencil;
        view_desc.dimension = RHITextureViewDimension::Texture2D;
        view_desc.format = PixelFormat::D32Float;
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
        pass_desc.depth_stencil_attachment.clear_value = RHIClearValue::DepthZero;
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
            RHIGraphicsBindings bindings;
            bindings.global = test_global_binding_set;
            bindings.view = test_view_binding_set;
            bindings.material = test_material_binding_set;
            status = context.bind_graphics_bindings(bindings);
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
