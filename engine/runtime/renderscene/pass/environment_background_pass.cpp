#include "renderscene/pass/environment_background_pass.h"

#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "math/quaternion.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_map.h"
#include "renderscene/render_scene.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/view/view_info.h"
#include "shader_parameters/toy3d_environment_background.generated.h"

namespace toy3d
{
    const GlobalShaderType& environment_background_global_shader_type()
    {
        static const EnvironmentBackgroundPassParameters parameters;
        const auto& metadata = shader_parameters_metadata(parameters);
        static const GlobalShaderType type(
            "EnvironmentBackgroundGlobalShader", "Toy3d/Environment/Background", "EnvironmentBackground",
            shader::default_shader_permutation_key, GlobalShaderType::ProgramKind::Graphics,
            RHIShaderStageFlags::Vertex | RHIShaderStageFlags::Pixel, metadata,
            {GlobalShaderBindingRequirement(metadata.constant_buffer.binding_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::UniformBuffer, 1, RHIShaderStageFlags::Pixel),
             GlobalShaderBindingRequirement(metadata.resources[0].parameter_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::SampledTexture, 1, RHIShaderStageFlags::Pixel),
             GlobalShaderBindingRequirement(metadata.resources[1].parameter_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::Sampler, 1, RHIShaderStageFlags::Pixel)});
        return type;
    }

    namespace
    {
        const GlobalShaderTypeRegistration registration(environment_background_global_shader_type());
    }

    // --------------------------------------------------------------------------
    // EnvironmentBackgroundPassResources: depth-masked HDR background drawing
    // --------------------------------------------------------------------------
    RHIStatus EnvironmentBackgroundPassResources::initialize(RHIDevice& device, RHIShaderProgramCache& cache,
                                                             const GlobalShaderMap& shaders)
    {
        const auto found = shaders.find(environment_background_global_shader_type());
        if (!found.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Environment background Shader: " + found.error);
        }
        const auto program = cache.find_or_create(found.program);
        if (!program)
        {
            return program.status();
        }
        RHIGraphicsPipelineDesc desc;
        desc.vertex_shader = program.value()->vertex_shader;
        desc.pixel_shader = program.value()->pixel_shader;
        desc.binding_layout = program.value()->binding_layout;
        desc.primitive_topology = RHIPrimitiveTopology::TriangleList;
        desc.color_attachment_count = 1;
        desc.color_formats[0] = PixelFormat::R16G16B16A16Float;
        desc.depth_stencil_format = PixelFormat::D32Float;
        desc.debug_name = "EnvironmentBackgroundPipeline";
        const auto translated = build_shader_graphics_pipeline_desc(desc, found.program->data().graphics_pass_state);
        if (!translated)
        {
            return translated.status();
        }
        const auto& depth = translated.value().depth_stencil;
        if (!depth.depth_test_enable || depth.depth_write_enable || depth.stencil_test_enable ||
            depth.depth_compare_operation != RHICompareOperation::Equal)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Environment background requires read-only equal depth.");
        }
        const auto pipeline = device.create_graphics_pipeline(translated.value());
        if (!pipeline)
        {
            return pipeline.status();
        }
        program_ = program.value();
        pipeline_ = pipeline.value();
        return RHIStatus::success();
    }

    RHIStatus EnvironmentBackgroundPassResources::render(RHIDevice& device, RHIGraphicsCommandContext& context,
                                                         RenderScene& scene, const SceneRenderTargets& targets,
                                                         const ViewInfo& view) const
    {
        const auto& environment = scene.environment_for_current_recording();
        if (!environment.cube)
        {
            return RHIStatus::success();
        }
        if (!program_ || !pipeline_ || !targets.scene_color_view() || !targets.scene_depth_view())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Environment background is not prepared.");
        }
        const auto sampler = scene.environment_sampler(device);
        if (!sampler)
        {
            return sampler.status();
        }
        EnvironmentBackgroundPassParameters parameters;
        parameters.inverse_projection = view.inverse_projection_matrix();
        parameters.view_to_cube = to_matrix4(conjugate(environment.rotation)) * view.inverse_view_matrix();
        parameters.environment_intensity = environment.intensity;
        parameters.background_cube = scene.environment_view_for_current_recording();
        if (!parameters.background_cube)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady, "Preview environment Cube upload is pending.");
        }
        parameters.background_sampler = sampler.value();
        const auto binding = create_transient_shader_binding(device, context, parameters);
        if (!binding)
        {
            return binding.status();
        }
        // Publish Base attachment writes through read access before loading them.
        // The public RHI requires distinct transition states, so return both resources
        // to attachment access explicitly before the background pass begins.
        RHIResourceTransition color_dependency;
        color_dependency.resource = targets.scene_color_texture();
        color_dependency.subresources = targets.scene_color_view()->desc().subresources;
        color_dependency.before = RHIAccess::RenderTarget;
        color_dependency.after = RHIAccess::ShaderResourceGraphics;
        RHIResourceTransition depth_dependency;
        depth_dependency.resource = targets.scene_depth_texture();
        depth_dependency.subresources = targets.scene_depth_view()->desc().subresources;
        depth_dependency.before = RHIAccess::DepthStencilWrite;
        depth_dependency.after = RHIAccess::DepthStencilRead;
        auto status = context.transition_resources({color_dependency, depth_dependency});
        if (!status)
        {
            return status;
        }
        std::swap(color_dependency.before, color_dependency.after);
        std::swap(depth_dependency.before, depth_dependency.after);
        status = context.transition_resources({color_dependency, depth_dependency});
        if (!status)
        {
            return status;
        }
        RHIRenderPassDesc pass;
        RHIColorAttachmentDesc color;
        color.view = targets.scene_color_view();
        color.load = RHILoadOperation::Load;
        color.store = RHIStoreOperation::Store;
        pass.color_attachments.push_back(color);
        pass.has_depth_stencil_attachment = true;
        pass.depth_stencil_attachment.view = targets.scene_depth_view();
        pass.depth_stencil_attachment.depth_load = RHILoadOperation::Load;
        pass.depth_stencil_attachment.depth_store = RHIStoreOperation::Store;
        pass.depth_stencil_attachment.stencil_load = RHILoadOperation::Discard;
        pass.depth_stencil_attachment.stencil_store = RHIStoreOperation::Discard;
        pass.debug_name = "EnvironmentBackgroundPass";
        status = context.begin_render_pass(pass);
        if (!status)
        {
            return status;
        }
        status = context.set_graphics_pipeline(pipeline_);
        if (!status)
        {
            return status;
        }
        status = context.set_vertex_buffers({});
        if (!status)
        {
            return status;
        }
        const auto& rect = view.scene_view().view_rect();
        RHIViewport viewport;
        viewport.x = static_cast<float>(rect.x);
        viewport.y = static_cast<float>(rect.y);
        viewport.width = static_cast<float>(rect.width);
        viewport.height = static_cast<float>(rect.height);
        status = context.set_viewport(viewport);
        if (!status)
        {
            return status;
        }
        RHIRect scissor;
        scissor.x = rect.x;
        scissor.y = rect.y;
        scissor.width = rect.width;
        scissor.height = rect.height;
        status = context.set_scissor(scissor);
        if (!status)
        {
            return status;
        }
        RHIGraphicsBindings bindings;
        bindings.pass = binding.value();
        status = context.bind_graphics_bindings(bindings);
        if (!status)
        {
            return status;
        }
        RHIDrawArgs draw;
        draw.vertex_count = 3;
        status = context.draw(draw);
        if (!status)
        {
            return status;
        }
        return context.end_render_pass();
    }
} // namespace toy3d
