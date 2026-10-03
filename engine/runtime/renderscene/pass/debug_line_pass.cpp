#include "renderscene/pass/debug_line_pass.h"

#include <cstddef>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/global_shader_type_registry.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/shader/shader_map.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/view/view_info.h"
#include "shader_parameters/toy3d_debug_lines.generated.h"

namespace toy3d
{
    const GlobalShaderType& debug_lines_global_shader_type()
    {
        static const DebugLinesPassParameters parameters;
        const auto& metadata = shader_parameters_metadata(parameters);
        static const GlobalShaderType type(
            "DebugLinesGlobalShader", "Toy3d/Debug/Lines", "DebugLines", shader::default_shader_permutation_key,
            GlobalShaderType::ProgramKind::Graphics, RHIShaderStageFlags::Vertex | RHIShaderStageFlags::Pixel, metadata,
            {GlobalShaderBindingRequirement(metadata.constant_buffer.binding_id, RHIBindingGroup::Pass,
                                            RHIResourceBindingType::UniformBuffer, 1, RHIShaderStageFlags::Vertex)});
        return type;
    }

    namespace
    {
        const GlobalShaderTypeRegistration registration(debug_lines_global_shader_type());
    }

    // --------------------------------------------------------------------------
    // DebugLinePassResources: public-RHI world-space line overlay
    // --------------------------------------------------------------------------
    RHIStatus DebugLinePassResources::initialize(RHIDevice& device, RHIShaderProgramCache& cache,
                                                 const GlobalShaderMap& shaders)
    {
        const auto found = shaders.find(debug_lines_global_shader_type());
        if (!found.succeeded())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, found.error);
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
        desc.primitive_topology = RHIPrimitiveTopology::LineList;
        desc.color_attachment_count = 1;
        desc.color_formats[0] = PixelFormat::R16G16B16A16Float;
        desc.depth_stencil_format = PixelFormat::D32Float;
        desc.vertex_buffers.push_back({0, sizeof(DebugLineVertex), RHIVertexInputRate::PerVertex});
        for (const auto& input : found.program->data().vertex_inputs)
        {
            RHIGraphicsPipelineDesc::VertexAttribute attribute;
            attribute.location = input.target_location;
            if (input.attribute_id == ShaderVertexAttributeId::Position0)
            {
                attribute.format = PixelFormat::R32G32B32A32Float;
                attribute.offset = offsetof(DebugLineVertex, position);
            }
            else if (input.attribute_id == ShaderVertexAttributeId::Color0)
            {
                attribute.format = PixelFormat::R32G32B32A32Float;
                attribute.offset = offsetof(DebugLineVertex, color);
            }
            else
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Unexpected debug line vertex input.");
            }
            desc.vertex_attributes.push_back(attribute);
        }
        auto translated = build_shader_graphics_pipeline_desc(desc, found.program->data().graphics_pass_state);
        if (!translated)
        {
            return translated.status();
        }
        desc = translated.value();
        desc.debug_name = "DebugLinesDepth";
        const auto depth = device.create_graphics_pipeline(desc);
        if (!depth)
        {
            return depth.status();
        }
        desc.depth_stencil.depth_test_enable = false;
        desc.debug_name = "DebugLinesOverlay";
        const auto overlay = device.create_graphics_pipeline(desc);
        if (!overlay)
        {
            return overlay.status();
        }
        program_ = program.value();
        depth_pipeline_ = depth.value();
        overlay_pipeline_ = overlay.value();
        return RHIStatus::success();
    }

    RHIStatus DebugLinePassResources::render(RHIDevice& device, RHIGraphicsCommandContext& context,
                                             const SceneRenderTargets& targets, const ViewInfo& view,
                                             const std::vector<DebugLineVertex>& vertices, bool depth_test) const
    {
        if (vertices.empty())
        {
            return RHIStatus::success();
        }
        // Bound the transient upload and reject incomplete segments before recording.
        if (!program_ || vertices.size() % 2 != 0 || vertices.size() > 65536)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Invalid debug line payload.");
        }
        for (const auto& vertex : vertices)
        {
            if (!is_finite(vertex.position) || !is_finite(vertex.color))
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Debug line coordinates/color must be finite.");
            }
        }
        RHIBufferDesc desc;
        desc.size = vertices.size() * sizeof(DebugLineVertex);
        desc.usage = RHIResourceUsage::VertexBuffer | RHIResourceUsage::CopyDestination;
        desc.initial_access = RHIAccess::Common;
        desc.debug_name = "PreviewBoneLines";
        const auto buffer = device.create_buffer(desc);
        if (!buffer)
        {
            return buffer.status();
        }
        RHIResourceTransition upload_transition;
        upload_transition.resource = buffer.value();
        upload_transition.before = RHIAccess::Common;
        upload_transition.after = RHIAccess::CopyDestination;
        auto status = context.transition_resources({upload_transition});
        if (!status)
        {
            return status;
        }
        RHIBufferUploadDesc upload;
        upload.destination = buffer.value();
        upload.source.data = vertices.data();
        upload.source.size = desc.size;
        status = context.upload_buffer(upload);
        if (!status)
        {
            return status;
        }
        upload_transition.before = RHIAccess::CopyDestination;
        upload_transition.after = RHIAccess::VertexBuffer;
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
        status = context.transition_resources({upload_transition, color_dependency, depth_dependency});
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
        DebugLinesPassParameters parameters;
        parameters.view_projection = view.view_projection_matrix();
        const auto binding = create_transient_shader_binding(device, context, parameters);
        if (!binding)
        {
            return binding.status();
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
        pass.debug_name = "PreviewBoneLines";
        status = context.begin_render_pass(pass);
        if (!status)
        {
            return status;
        }
        status = context.set_graphics_pipeline(depth_test ? depth_pipeline_ : overlay_pipeline_);
        if (!status)
        {
            return status;
        }
        status = context.set_vertex_buffers({{buffer.value(), 0, sizeof(DebugLineVertex)}});
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
        status = context.set_scissor({rect.x, rect.y, rect.width, rect.height});
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
        draw.vertex_count = static_cast<std::uint32_t>(vertices.size());
        status = context.draw(draw);
        return status ? context.end_render_pass() : status;
    }
} // namespace toy3d
