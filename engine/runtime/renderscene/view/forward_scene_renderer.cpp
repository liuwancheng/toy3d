#include "renderscene/view/forward_scene_renderer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "logging/logger.h"
#include "math/matrix_construction.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "rendercore/scene/static_mesh_scene_proxy.h"
#include "rendercore/shader/rhi_shader_program.h"
#include "rendercore/shader/primitive_uniform_shader_parameters.h"
#include "rendercore/shader/view_uniform_shader_parameters.h"
#include "renderscene/material/material_render_proxy.h"
#include "renderscene/geometry/static_mesh_render_data.h"
#include "renderscene/primitive_scene_info.h"
#include "renderscene/render_scene.h"
#include "renderscene/render_resource_manager.h"
#include "renderscene/scene_render_targets.h"

namespace toy3d
{
    namespace
    {
        bool program_declares_group(
            const ShaderMapProgram& program,
            RHIBindingGroup group)
        {
            for (const ShaderMapBinding& binding : program.data().bindings)
            {
                if (binding.group == group)
                {
                    return true;
                }
            }
            return false;
        }

        RHIPrimitiveTopology to_rhi_primitive_topology(
            shader::ShaderGraphicsPassState::PrimitiveTopology topology)
        {
            using Source = shader::ShaderGraphicsPassState::PrimitiveTopology;
            switch (topology)
            {
            case Source::PointList: return RHIPrimitiveTopology::PointList;
            case Source::LineList: return RHIPrimitiveTopology::LineList;
            case Source::LineStrip: return RHIPrimitiveTopology::LineStrip;
            case Source::TriangleList: return RHIPrimitiveTopology::TriangleList;
            case Source::TriangleStrip: return RHIPrimitiveTopology::TriangleStrip;
            }
            return RHIPrimitiveTopology::TriangleList;
        }

        RHICullMode to_rhi_cull_mode(
            shader::ShaderGraphicsPassState::CullMode mode)
        {
            using Source = shader::ShaderGraphicsPassState::CullMode;
            switch (mode)
            {
            case Source::None: return RHICullMode::None;
            case Source::Front: return RHICullMode::Front;
            case Source::Back: return RHICullMode::Back;
            }
            return RHICullMode::Back;
        }

        RHIFrontFace to_rhi_front_face(
            shader::ShaderGraphicsPassState::FrontFace face)
        {
            using Source = shader::ShaderGraphicsPassState::FrontFace;
            switch (face)
            {
            case Source::Clockwise: return RHIFrontFace::Clockwise;
            case Source::CounterClockwise: return RHIFrontFace::CounterClockwise;
            }
            return RHIFrontFace::CounterClockwise;
        }

        RHIPolygonMode to_rhi_polygon_mode(
            shader::ShaderGraphicsPassState::FillMode mode)
        {
            using Source = shader::ShaderGraphicsPassState::FillMode;
            switch (mode)
            {
            case Source::Solid: return RHIPolygonMode::Fill;
            case Source::Wireframe: return RHIPolygonMode::Line;
            }
            return RHIPolygonMode::Fill;
        }

        RHICompareOperation to_rhi_compare_operation(
            shader::ShaderGraphicsPassState::CompareOperation operation)
        {
            using Source = shader::ShaderGraphicsPassState::CompareOperation;
            switch (operation)
            {
            case Source::Never: return RHICompareOperation::Never;
            case Source::Less: return RHICompareOperation::Less;
            case Source::Equal: return RHICompareOperation::Equal;
            case Source::LessEqual: return RHICompareOperation::LessEqual;
            case Source::Greater: return RHICompareOperation::Greater;
            case Source::NotEqual: return RHICompareOperation::NotEqual;
            case Source::GreaterEqual: return RHICompareOperation::GreaterEqual;
            case Source::Always: return RHICompareOperation::Always;
            }
            return RHICompareOperation::Always;
        }

        RHIStencilOperation to_rhi_stencil_operation(
            shader::ShaderGraphicsPassState::StencilOperation operation)
        {
            using Source = shader::ShaderGraphicsPassState::StencilOperation;
            switch (operation)
            {
            case Source::Keep: return RHIStencilOperation::Keep;
            case Source::Zero: return RHIStencilOperation::Zero;
            case Source::Replace: return RHIStencilOperation::Replace;
            case Source::IncrementClamp: return RHIStencilOperation::IncrementClamp;
            case Source::DecrementClamp: return RHIStencilOperation::DecrementClamp;
            case Source::Invert: return RHIStencilOperation::Invert;
            case Source::IncrementWrap: return RHIStencilOperation::IncrementWrap;
            case Source::DecrementWrap: return RHIStencilOperation::DecrementWrap;
            }
            return RHIStencilOperation::Keep;
        }

        RHIBlendFactor to_rhi_blend_factor(
            shader::ShaderGraphicsPassState::BlendFactor factor)
        {
            using Source = shader::ShaderGraphicsPassState::BlendFactor;
            switch (factor)
            {
            case Source::Zero: return RHIBlendFactor::Zero;
            case Source::One: return RHIBlendFactor::One;
            case Source::SourceColor: return RHIBlendFactor::SourceColor;
            case Source::OneMinusSourceColor:
                return RHIBlendFactor::OneMinusSourceColor;
            case Source::DestinationColor: return RHIBlendFactor::DestinationColor;
            case Source::OneMinusDestinationColor:
                return RHIBlendFactor::OneMinusDestinationColor;
            case Source::SourceAlpha: return RHIBlendFactor::SourceAlpha;
            case Source::OneMinusSourceAlpha:
                return RHIBlendFactor::OneMinusSourceAlpha;
            case Source::DestinationAlpha: return RHIBlendFactor::DestinationAlpha;
            case Source::OneMinusDestinationAlpha:
                return RHIBlendFactor::OneMinusDestinationAlpha;
            case Source::ConstantColor: return RHIBlendFactor::ConstantColor;
            case Source::OneMinusConstantColor:
                return RHIBlendFactor::OneMinusConstantColor;
            case Source::SourceAlphaSaturate:
                return RHIBlendFactor::SourceAlphaSaturate;
            }
            return RHIBlendFactor::One;
        }

        RHIBlendOperation to_rhi_blend_operation(
            shader::ShaderGraphicsPassState::BlendOperation operation)
        {
            using Source = shader::ShaderGraphicsPassState::BlendOperation;
            switch (operation)
            {
            case Source::Add: return RHIBlendOperation::Add;
            case Source::Subtract: return RHIBlendOperation::Subtract;
            case Source::ReverseSubtract:
                return RHIBlendOperation::ReverseSubtract;
            case Source::Minimum: return RHIBlendOperation::Min;
            case Source::Maximum: return RHIBlendOperation::Max;
            }
            return RHIBlendOperation::Add;
        }

        RHIColorWriteMask to_rhi_color_write_mask(
            shader::ShaderGraphicsPassState::ColorWriteMask mask)
        {
            using Source = shader::ShaderGraphicsPassState::ColorWriteMask;
            switch (mask)
            {
            case Source::None: return RHIColorWriteMask::None;
            case Source::Red: return RHIColorWriteMask::Red;
            case Source::Green: return RHIColorWriteMask::Green;
            case Source::Blue: return RHIColorWriteMask::Blue;
            case Source::Alpha: return RHIColorWriteMask::Alpha;
            case Source::RedGreen:
                return RHIColorWriteMask::Red | RHIColorWriteMask::Green;
            case Source::RedGreenBlue:
                return RHIColorWriteMask::Red |
                    RHIColorWriteMask::Green |
                    RHIColorWriteMask::Blue;
            case Source::All: return RHIColorWriteMask::All;
            }
            return RHIColorWriteMask::All;
        }

        void apply_stencil_face_state(
            const shader::ShaderGraphicsPassState::StencilFaceState& source,
            RHIGraphicsPipelineDesc::StencilFaceState& destination)
        {
            destination.compare_operation =
                to_rhi_compare_operation(source.compare_operation);
            destination.fail_operation =
                to_rhi_stencil_operation(source.fail_operation);
            destination.depth_fail_operation =
                to_rhi_stencil_operation(source.depth_fail_operation);
            destination.pass_operation =
                to_rhi_stencil_operation(source.pass_operation);
        }

        void apply_graphics_pass_state(
            const shader::ShaderGraphicsPassState& source,
            RHIGraphicsPipelineDesc& destination)
        {
            destination.primitive_topology =
                to_rhi_primitive_topology(source.primitive_topology);
            destination.rasterization.polygon_mode =
                to_rhi_polygon_mode(source.fill_mode);
            destination.rasterization.cull_mode =
                to_rhi_cull_mode(source.cull_mode);
            destination.rasterization.front_face =
                to_rhi_front_face(source.front_face);
            destination.depth_stencil.depth_test_enable =
                source.depth_test_enable;
            destination.depth_stencil.depth_write_enable =
                source.depth_write_enable;
            destination.depth_stencil.depth_compare_operation =
                to_rhi_compare_operation(source.depth_compare_operation);
            destination.depth_stencil.stencil_test_enable =
                source.stencil.mode !=
                shader::ShaderGraphicsPassState::StencilMode::Off;
            destination.depth_stencil.stencil_read_mask =
                source.stencil.read_mask;
            destination.depth_stencil.stencil_write_mask =
                source.stencil.write_mask;
            apply_stencil_face_state(
                source.stencil.front, destination.depth_stencil.front_face);
            apply_stencil_face_state(
                source.stencil.back, destination.depth_stencil.back_face);

            RHIGraphicsPipelineDesc::ColorBlendAttachmentState blend;
            blend.blend_enable = source.blend.enabled;
            blend.source_color_factor =
                to_rhi_blend_factor(source.blend.source_color_factor);
            blend.destination_color_factor =
                to_rhi_blend_factor(source.blend.destination_color_factor);
            blend.color_operation =
                to_rhi_blend_operation(source.blend.color_operation);
            blend.source_alpha_factor =
                to_rhi_blend_factor(source.blend.source_alpha_factor);
            blend.destination_alpha_factor =
                to_rhi_blend_factor(source.blend.destination_alpha_factor);
            blend.alpha_operation =
                to_rhi_blend_operation(source.blend.alpha_operation);
            blend.color_write_mask =
                to_rhi_color_write_mask(source.color_write_mask);
            for (std::uint32_t index = 0;
                 index < destination.color_attachment_count;
                 ++index)
            {
                destination.color_blend_attachments[index] = blend;
            }
        }

        RHIStatus apply_attachment_compatibility(
            const RHIRenderPassDesc& pass_desc,
            RHIGraphicsPipelineDesc& pipeline_desc)
        {
            if (pass_desc.color_attachments.empty() ||
                !pass_desc.has_depth_stencil_attachment)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Forward Base Pass requires color and depth attachments.");
            }
            if (pass_desc.color_attachments.size() >
                pipeline_desc.color_formats.size())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Forward Base Pass has too many color attachments.");
            }

            pipeline_desc.color_attachment_count =
                static_cast<std::uint32_t>(
                    pass_desc.color_attachments.size());
            for (std::size_t index = 0;
                 index < pass_desc.color_attachments.size();
                 ++index)
            {
                const RHITextureViewRef& view =
                    pass_desc.color_attachments[index].view;
                if (!view || !view->texture())
                {
                    return RHIStatus::failure(
                        RHIErrorCode::InvalidArgument,
                        "Forward Base Pass color attachment is unavailable.");
                }
                pipeline_desc.color_formats[index] = view->desc().format;
                if (index == 0)
                {
                    pipeline_desc.sample_count =
                        view->texture()->desc().sample_count;
                }
            }

            const RHITextureViewRef& depth_view =
                pass_desc.depth_stencil_attachment.view;
            if (!depth_view || !depth_view->texture())
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Forward Base Pass depth attachment is unavailable.");
            }
            pipeline_desc.depth_stencil_format = depth_view->desc().format;
            return RHIStatus::success();
        }
    }

    ForwardSceneRenderer::ForwardSceneRenderer(SceneViewFamily view_family)
        : SceneRenderer(std::move(view_family))
    {}

    RHIResult<RHIFrameEndResult> ForwardSceneRenderer::render_frame(
        RenderScene& render_scene,
        RHIDevice& device,
        RenderResourceManager& resource_manager,
        RHIViewportContext& viewport,
        SceneRenderTargets& scene_render_targets)
    {
        RHIResult<std::unique_ptr<RHIFrameContext>> frame_result =
            viewport.begin_frame();
        if (!frame_result)
        {
            return RHIResult<RHIFrameEndResult>::failure(
                frame_result.status().code(),
                frame_result.status().message());
        }

        std::unique_ptr<RHIFrameContext> frame =
            std::move(frame_result).value();
        if (!frame)
        {
            return RHIResult<RHIFrameEndResult>::failure(
                RHIErrorCode::BackendFailure,
                "Viewport begin_frame succeeded without a frame context.");
        }

        bool resource_recording_started = false;
        const auto abort_recording =
            [&resource_manager, &viewport, &frame,
             &resource_recording_started](const RHIStatus& failure)
                -> RHIResult<RHIFrameEndResult>
            {
                RHIStatus discard_status = RHIStatus::success();
                if (resource_recording_started)
                {
                    discard_status =
                        resource_manager.discard_recording();
                    if (!discard_status)
                    {
                        TOY_LOG_ERROR(
                            "Forward frame could not discard its RenderResource recording after '{}': {}",
                            failure.message(), discard_status.message());
                    }
                }

                const RHIStatus abort_status =
                    viewport.abort_frame(std::move(frame));
                if (!abort_status)
                {
                    TOY_LOG_ERROR(
                        "Forward frame abort failed after '{}': {}",
                        failure.message(), abort_status.message());
                    return RHIResult<RHIFrameEndResult>::failure(
                        abort_status.code(), abort_status.message());
                }
                if (!discard_status)
                {
                    return RHIResult<RHIFrameEndResult>::failure(
                        discard_status.code(), discard_status.message());
                }
                return RHIResult<RHIFrameEndResult>::failure(
                    failure.code(), failure.message());
            };

        if (!frame->present_texture() || !frame->present_view())
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward frame requires present attachments."));
        }
        if (!frame->present_texture()->is_owned_by(device) ||
            !frame->present_view()->is_owned_by(device) ||
            frame->present_view()->texture() != frame->present_texture())
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward frame present attachments must belong to the injected device and current frame."));
        }
        if (view_family().output_size() !=
            UIntVector2(frame->width(), frame->height()))
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward frame View family output does not match the acquired frame extent."));
        }
        RHIStatus status = scene_render_targets.ensure_extent(
            device,
            frame->width(),
            frame->height(),
            frame->present_texture()->desc().format);
        if (!status)
        {
            return abort_recording(status);
        }
        const bool scene_targets_complete =
            scene_render_targets.scene_color_texture() &&
            scene_render_targets.scene_color_view() &&
            scene_render_targets.scene_color_shader_resource_view() &&
            scene_render_targets.scene_depth_texture() &&
            scene_render_targets.scene_depth_view() &&
            scene_render_targets.scene_depth_shader_resource_view();
        const bool scene_targets_owned = scene_targets_complete &&
            scene_render_targets.scene_color_texture()->is_owned_by(device) &&
            scene_render_targets.scene_color_view()->is_owned_by(device) &&
            scene_render_targets.scene_color_shader_resource_view()
                ->is_owned_by(device) &&
            scene_render_targets.scene_depth_texture()->is_owned_by(device) &&
            scene_render_targets.scene_depth_view()->is_owned_by(device) &&
            scene_render_targets.scene_depth_shader_resource_view()
                ->is_owned_by(device);
        if (!scene_targets_owned)
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward frame requires complete SceneRenderTargets owned by the injected device."));
        }

        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> context_result =
            frame->create_graphics_command_context();
        if (!context_result)
        {
            return abort_recording(context_result.status());
        }
        std::unique_ptr<RHIGraphicsCommandContext> context =
            std::move(context_result).value();
        if (!context)
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Viewport frame created no graphics command context."));
        }

        status = context->begin_recording("ForwardSceneRenderer");
        if (!status)
        {
            return abort_recording(status);
        }

        resource_recording_started = true;
        status = resource_manager.record_pending_uploads(*context);
        if (!status)
        {
            return abort_recording(status);
        }
        if (!init_views())
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward frame rejected its SceneViewFamily inputs."));
        }

        compute_view_visibility(render_scene);
        collect_mesh_batches();

        std::vector<RHIResourceTransition> scene_attachment_transitions;
        if (scene_render_targets.scene_color_access() != RHIAccess::RenderTarget)
        {
            RHIResourceTransition scene_color_to_render_target;
            scene_color_to_render_target.resource =
                scene_render_targets.scene_color_texture();
            scene_color_to_render_target.subresources =
                scene_render_targets.scene_color_view()->desc().subresources;
            scene_color_to_render_target.before =
                scene_render_targets.scene_color_access();
            scene_color_to_render_target.after = RHIAccess::RenderTarget;
            scene_attachment_transitions.push_back(
                std::move(scene_color_to_render_target));
        }
        if (scene_render_targets.scene_depth_access() !=
            RHIAccess::DepthStencilWrite)
        {
            RHIResourceTransition scene_depth_to_write;
            scene_depth_to_write.resource =
                scene_render_targets.scene_depth_texture();
            scene_depth_to_write.subresources =
                scene_render_targets.scene_depth_view()->desc().subresources;
            scene_depth_to_write.before =
                scene_render_targets.scene_depth_access();
            scene_depth_to_write.after = RHIAccess::DepthStencilWrite;
            scene_attachment_transitions.push_back(
                std::move(scene_depth_to_write));
        }
        if (!scene_attachment_transitions.empty())
        {
            status = context->transition_resources(scene_attachment_transitions);
        }
        if (!status)
        {
            return abort_recording(status);
        }

        RHIRenderPassDesc pass_desc;
        RHIColorAttachmentDesc color_attachment;
        color_attachment.view = scene_render_targets.scene_color_view();
        color_attachment.load = RHILoadOperation::Clear;
        color_attachment.store = RHIStoreOperation::Store;
        color_attachment.clear_value =
            RHIClearValue::color_value(vec4(0.0F, 0.0F, 0.0F, 1.0F));
        pass_desc.color_attachments.push_back(std::move(color_attachment));
        pass_desc.has_depth_stencil_attachment = true;
        pass_desc.depth_stencil_attachment.view =
            scene_render_targets.scene_depth_view();
        pass_desc.depth_stencil_attachment.depth_load =
            RHILoadOperation::Clear;
        pass_desc.depth_stencil_attachment.depth_store =
            RHIStoreOperation::Store;
        pass_desc.depth_stencil_attachment.stencil_load =
            RHILoadOperation::Discard;
        pass_desc.depth_stencil_attachment.stencil_store =
            RHIStoreOperation::Discard;
        pass_desc.depth_stencil_attachment.clear_value =
            RHIClearValue::DepthZero;
        pass_desc.debug_name = "ForwardBasePass";
        status = render_base_pass(device, *context, pass_desc);
        if (!status)
        {
            return abort_recording(status);
        }

        std::vector<RHIResourceTransition> backbuffer_copy_transitions;
        RHIResourceTransition scene_color_to_copy_source;
        scene_color_to_copy_source.resource =
            scene_render_targets.scene_color_texture();
        scene_color_to_copy_source.subresources =
            scene_render_targets.scene_color_view()->desc().subresources;
        scene_color_to_copy_source.before = RHIAccess::RenderTarget;
        scene_color_to_copy_source.after = RHIAccess::CopySource;
        backbuffer_copy_transitions.push_back(
            std::move(scene_color_to_copy_source));

        RHIResourceTransition present_to_copy_destination;
        present_to_copy_destination.resource = frame->present_texture();
        present_to_copy_destination.subresources =
            frame->present_view()->desc().subresources;
        present_to_copy_destination.before = RHIAccess::Present;
        present_to_copy_destination.after = RHIAccess::CopyDestination;
        backbuffer_copy_transitions.push_back(
            std::move(present_to_copy_destination));

        status = context->transition_resources(backbuffer_copy_transitions);
        if (!status)
        {
            return abort_recording(status);
        }

        RHITextureCopyDesc scene_color_copy;
        scene_color_copy.source.texture =
            scene_render_targets.scene_color_texture();
        scene_color_copy.destination.texture = frame->present_texture();
        scene_color_copy.extent = {frame->width(), frame->height(), 1u};
        status = context->copy_texture(scene_color_copy);
        if (!status)
        {
            return abort_recording(status);
        }

        RHIResourceTransition backbuffer_to_ui_render_target;
        backbuffer_to_ui_render_target.resource = frame->present_texture();
        backbuffer_to_ui_render_target.subresources =
            frame->present_view()->desc().subresources;
        backbuffer_to_ui_render_target.before = RHIAccess::CopyDestination;
        backbuffer_to_ui_render_target.after = RHIAccess::RenderTarget;
        status = context->transition_resources({backbuffer_to_ui_render_target});
        if (!status)
        {
            return abort_recording(status);
        }

        RHIRenderPassDesc ui_pass_desc;
        RHIColorAttachmentDesc ui_color_attachment;
        ui_color_attachment.view = frame->present_view();
        ui_color_attachment.load = RHILoadOperation::Load;
        ui_color_attachment.store = RHIStoreOperation::Store;
        ui_pass_desc.color_attachments.push_back(
            std::move(ui_color_attachment));
        ui_pass_desc.debug_name = "UIOverlayPass";
        status = context->begin_render_pass(ui_pass_desc);
        if (status)
        {
            status = context->end_render_pass();
        }
        if (!status)
        {
            return abort_recording(status);
        }

        RHIResourceTransition render_target_to_present;
        render_target_to_present.resource = frame->present_texture();
        render_target_to_present.subresources =
            frame->present_view()->desc().subresources;
        render_target_to_present.before = RHIAccess::RenderTarget;
        render_target_to_present.after = RHIAccess::Present;
        status = context->transition_resources({render_target_to_present});
        if (!status)
        {
            return abort_recording(status);
        }

        RHIResult<RHICommandListRef> command_list_result =
            context->finish_recording();
        if (!command_list_result)
        {
            return abort_recording(command_list_result.status());
        }
        RHICommandListRef command_list =
            std::move(command_list_result).value();
        if (!command_list)
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Forward frame finished without an immutable command list."));
        }

        RHIResult<RHIFrameEndResult> end_result = viewport.end_frame(
            std::move(frame), {std::move(command_list)});
        if (!end_result)
        {
            const RHIStatus discard_status =
                resource_manager.discard_recording();
            if (!discard_status)
            {
                TOY_LOG_ERROR(
                    "Forward frame submit failed and its RenderResource recording could not be discarded: {}",
                    discard_status.message());
                return RHIResult<RHIFrameEndResult>::failure(
                    discard_status.code(), discard_status.message());
            }
            return RHIResult<RHIFrameEndResult>::failure(
                end_result.status().code(), end_result.status().message());
        }

        RHIFrameEndResult submitted_result =
            std::move(end_result).value();
        if (submitted_result.completion_value == 0u)
        {
            // end_frame outer success has already established submit truth, so
            // an invalid completion is terminal metadata rather than a reason
            // to report the business work as unsubmitted.
            submitted_result.presentation_status = RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Forward frame submit returned an invalid completion value.");
        }
        const RHIStatus commit_status = resource_manager.commit_recording();
        if (!commit_status)
        {
            // Business work is already submitted. Preserve that truth and its
            // completion value while surfacing a terminal status; never roll
            // the resource/RHI transaction back after native submit.
            TOY_LOG_ERROR(
                "Forward frame submitted but RenderResource publication failed: {}",
                commit_status.message());
            if (submitted_result.presentation_status.succeeded() ||
                rhi_is_recoverable_viewport_status(
                    submitted_result.presentation_status))
            {
                submitted_result.presentation_status = commit_status;
            }
        }
        scene_render_targets.publish_submitted_access(
            RHIAccess::CopySource,
            RHIAccess::DepthStencilWrite);
        return RHIResult<RHIFrameEndResult>::success(
            std::move(submitted_result));
    }

    bool ForwardSceneRenderer::init_views()
    {
        view_infos().clear();
        const UIntVector2 family_output_size = view_family().output_size();
        if (family_output_size.x == 0 || family_output_size.y == 0)
        {
            TOY_LOG_ERROR(
                "ForwardSceneRenderer init_views requires a non-empty family output.");
            return false;
        }

        std::vector<ViewInfo> initialized_views;
        initialized_views.reserve(view_family().views().size());
        for (std::size_t view_index = 0;
             view_index < view_family().views().size();
             ++view_index)
        {
            const SceneView& scene_view = view_family().views()[view_index];
            const UIntVector2 output_size = scene_view.output_size();
            const UIntVector2 rect_minimum = scene_view.view_rect_minimum();
            const UIntVector2 rect_size = scene_view.view_rect_size();
            if (output_size.x == 0 || output_size.y == 0 ||
                output_size != family_output_size)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with invalid or inconsistent output size.",
                    view_index);
                return false;
            }
            if (rect_size.x == 0 || rect_size.y == 0 ||
                rect_minimum.x >= output_size.x ||
                rect_minimum.y >= output_size.y ||
                rect_size.x > output_size.x - rect_minimum.x ||
                rect_size.y > output_size.y - rect_minimum.y)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with an empty or out-of-bounds view rect.",
                    view_index);
                return false;
            }
            if (!is_finite(scene_view.camera_position()) ||
                !is_finite(scene_view.camera_orientation()) ||
                !is_finite(scene_view.camera_direction()) ||
                !is_finite(scene_view.near_clip()) ||
                scene_view.near_clip() <= 0.0f)
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected View {} with non-finite camera input or a non-positive near plane.",
                    view_index);
                return false;
            }

            Matrix4 view_matrix;
            if (!try_make_view_matrix(
                    scene_view.camera_position(),
                    scene_view.camera_orientation(),
                    view_matrix))
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views failed to construct View {} world-to-view matrix.",
                    view_index);
                return false;
            }

            const float aspect =
                static_cast<float>(rect_size.x) /
                static_cast<float>(rect_size.y);
            Matrix4 projection_matrix;
            switch (scene_view.projection_mode())
            {
            case CameraProjectionMode::Perspective:
            {
                PerspectiveProjectionDesc projection_desc;
                projection_desc.vertical_fov = scene_view.vertical_fov();
                projection_desc.aspect = aspect;
                projection_desc.near_clip = scene_view.near_clip();
                projection_desc.far_clip = scene_view.far_clip();
                if (!try_make_perspective_projection(
                        projection_desc, projection_matrix))
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer init_views rejected View {} finite perspective inputs.",
                        view_index);
                    return false;
                }
                break;
            }
            case CameraProjectionMode::PerspectiveInfiniteFar:
            {
                InfinitePerspectiveProjectionDesc projection_desc;
                projection_desc.vertical_fov = scene_view.vertical_fov();
                projection_desc.aspect = aspect;
                projection_desc.near_clip = scene_view.near_clip();
                if (!try_make_infinite_perspective_projection(
                        projection_desc, projection_matrix))
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer init_views rejected View {} infinite-far perspective inputs.",
                        view_index);
                    return false;
                }
                break;
            }
            case CameraProjectionMode::Orthographic:
            case CameraProjectionMode::Custom:
            default:
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected unsupported projection mode for View {}.",
                    view_index);
                return false;
            }

            const Matrix4 view_projection_matrix =
                projection_matrix * view_matrix;
            Matrix4 inverse_view_matrix;
            Matrix4 inverse_projection_matrix;
            Matrix4 inverse_view_projection_matrix;
            if (!is_finite(view_matrix) ||
                !is_finite(projection_matrix) ||
                !is_finite(view_projection_matrix) ||
                !try_inverse(view_matrix, inverse_view_matrix) ||
                !try_inverse(projection_matrix, inverse_projection_matrix) ||
                !try_inverse(
                    view_projection_matrix,
                    inverse_view_projection_matrix))
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected non-finite or non-invertible derived matrices for View {}.",
                    view_index);
                return false;
            }

            ConvexVolume view_frustum;
            if (!try_make_reversed_z_frustum(
                    view_projection_matrix,
                    scene_view.infinite_far(),
                    view_frustum))
            {
                TOY_LOG_ERROR(
                    "ForwardSceneRenderer init_views rejected degenerate frustum planes for View {}.",
                    view_index);
                return false;
            }

            ViewInfo initialized_view(
                scene_view,
                std::move(view_matrix),
                std::move(projection_matrix),
                view_projection_matrix,
                std::move(inverse_view_matrix),
                std::move(inverse_projection_matrix),
                std::move(inverse_view_projection_matrix),
                std::move(view_frustum));
            initialized_views.push_back(std::move(initialized_view));
        }

        // A fresh vector makes each View's current-frame visibility start empty;
        // publication happens only after every View has initialized successfully.
        view_infos() = std::move(initialized_views);
        return true;
    }

    void ForwardSceneRenderer::compute_view_visibility(
        const RenderScene& render_scene)
    {
        for (ViewInfo& view_info : view_infos())
        {
            std::vector<PrimitiveSceneInfo*>& visible_primitives =
                view_info.visible_primitives_;
            visible_primitives.clear();
            visible_primitives.reserve(
                render_scene.primitive_scene_infos().size());

            for (const std::unique_ptr<PrimitiveSceneInfo>& primitive_info :
                 render_scene.primitive_scene_infos())
            {
                if (!primitive_info)
                {
                    continue;
                }

                PrimitiveSceneProxy* const proxy = primitive_info->proxy();
                if (proxy == nullptr || !proxy->visible())
                {
                    continue;
                }

                const AxisAlignedBounds& bounds = proxy->world_bounds();
                const Vector3 minimum(
                    bounds.minimum.x,
                    bounds.minimum.y,
                    bounds.minimum.z);
                const Vector3 maximum(
                    bounds.maximum.x,
                    bounds.maximum.y,
                    bounds.maximum.z);
                if (!view_info.view_frustum().intersects_axis_aligned_bounds(
                        minimum, maximum))
                {
                    continue;
                }

                visible_primitives.push_back(primitive_info.get());
            }
        }
    }

    void ForwardSceneRenderer::collect_mesh_batches()
    {
        for (ViewInfo& view_info : view_infos())
        {
            std::vector<MeshBatch>& mesh_batches = view_info.mesh_batches_;
            mesh_batches.clear();
            mesh_batches.reserve(view_info.visible_primitives_.size());

            for (PrimitiveSceneInfo* const primitive_info :
                 view_info.visible_primitives_)
            {
                if (primitive_info == nullptr)
                {
                    continue;
                }

                const auto* const static_mesh_proxy =
                    dynamic_cast<const StaticMeshSceneProxy*>(
                        primitive_info->proxy());
                if (static_mesh_proxy == nullptr)
                {
                    continue;
                }

                StaticMeshRenderData* const render_data =
                    static_mesh_proxy->render_data();
                if (render_data == nullptr)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh with no StaticMeshRenderData.");
                    continue;
                }
                const RHIStatus prepared_render_data =
                    render_data->prepare_current_recording();
                if (!prepared_render_data)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh whose render data is not ready in the current recording: {}",
                        prepared_render_data.message());
                    continue;
                }
                if (!render_data->is_drawable())
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh whose complete render-data gate is not drawable.");
                    continue;
                }

                const LocalVertexFactory* const vertex_factory =
                    render_data->vertex_factory();
                if (vertex_factory == nullptr)
                {
                    TOY_LOG_ERROR(
                        "ForwardSceneRenderer skipped a visible StaticMesh with no LocalVertexFactory.");
                    continue;
                }

                const std::vector<MaterialRenderProxy*>& material_proxies =
                    static_mesh_proxy->material_render_proxies();
                const std::vector<StaticMeshSection>& sections =
                    render_data->sections();
                for (std::size_t section_index = 0;
                     section_index < sections.size();
                     ++section_index)
                {
                    const StaticMeshSection& section = sections[section_index];
                    const std::size_t first_index = section.first_index;
                    const std::size_t index_count = section.index_count;
                    if (index_count == 0u || index_count % 3u != 0u ||
                        first_index > render_data->index_count() ||
                        index_count > render_data->index_count() - first_index)
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} with an invalid index range.",
                            section_index);
                        continue;
                    }
                    if (section.material_slot >= material_proxies.size())
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} whose Material slot is out of range.",
                            section_index);
                        continue;
                    }

                    MaterialRenderProxy* const material_proxy =
                        material_proxies[section.material_slot];
                    if (material_proxy == nullptr ||
                        !material_proxy->shader_program())
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} with no usable Material binding.",
                            section_index);
                        continue;
                    }

                    std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>
                        vertex_layouts;
                    std::vector<RHIGraphicsPipelineDesc::VertexAttribute>
                        vertex_attributes;
                    std::vector<RHIVertexBufferBinding> vertex_bindings;
                    const RHIStatus vertex_status =
                        vertex_factory->build_vertex_input(
                            material_proxy->shader_program()->data().vertex_inputs,
                            vertex_layouts,
                            vertex_attributes,
                            vertex_bindings);
                    if (!vertex_status)
                    {
                        TOY_LOG_ERROR(
                            "ForwardSceneRenderer skipped StaticMesh section {} because LocalVertexFactory is incompatible with ShaderVertexInput: {}",
                            section_index,
                            vertex_status.message());
                        continue;
                    }

                    mesh_batches.emplace_back(
                        *static_mesh_proxy,
                        *render_data,
                        *vertex_factory,
                        *material_proxy,
                        section.first_index,
                        section.index_count);
                }
            }
        }
    }

    RHIStatus ForwardSceneRenderer::render_base_pass(
        RHIDevice& device,
        RHIGraphicsCommandContext& context,
        const RHIRenderPassDesc& pass_desc)
    {
        if (pass_desc.color_attachments.empty() ||
            !pass_desc.has_depth_stencil_attachment)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward Base Pass requires color and depth attachments.");
        }
        if (pass_desc.color_attachments.size() > RHI_MAX_COLOR_ATTACHMENTS)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Forward Base Pass has too many color attachments.");
        }
        const RHIStatus pass_validation =
            validate_render_pass_desc(pass_desc);
        if (!pass_validation)
        {
            return pass_validation;
        }

        std::unordered_map<const ShaderMapProgram*, RHIShaderProgram>
            rhi_programs;
        std::vector<RHIViewport> prepared_viewports;
        std::vector<RHIRect> prepared_scissors;
        std::vector<RHIGraphicsPipelineRef> prepared_pipelines;
        std::vector<std::vector<RHIVertexBufferBinding>>
            prepared_vertex_bindings;
        std::vector<RHIIndexBufferBinding> prepared_index_bindings;
        std::vector<RHIGraphicsBindings> prepared_bindings;
        std::vector<RHIDrawIndexedArgs> prepared_draw_args;
        for (std::size_t view_index = 0;
             view_index < view_infos().size();
             ++view_index)
        {
            const ViewInfo& view_info = view_infos()[view_index];
            const SceneView& scene_view = view_info.scene_view();
            const UIntVector2 rect_minimum =
                scene_view.view_rect_minimum();
            const UIntVector2 rect_size = scene_view.view_rect_size();
            if (rect_minimum.x > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
                rect_minimum.y > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()))
            {
                TOY_LOG_ERROR(
                    "Forward Base Pass skipped View {} because its scissor origin exceeds the RHI signed range.",
                    view_index);
                continue;
            }

            RHIViewport viewport;
            viewport.x = static_cast<float>(rect_minimum.x);
            viewport.y = static_cast<float>(rect_minimum.y);
            viewport.width = static_cast<float>(rect_size.x);
            viewport.height = static_cast<float>(rect_size.y);
            RHIRect scissor;
            scissor.x = static_cast<std::int32_t>(rect_minimum.x);
            scissor.y = static_cast<std::int32_t>(rect_minimum.y);
            scissor.width = rect_size.x;
            scissor.height = rect_size.y;

            for (std::size_t batch_index = 0;
                 batch_index < view_info.mesh_batches().size();
                 ++batch_index)
            {
                const MeshBatch& mesh_batch =
                    view_info.mesh_batches()[batch_index];
                MaterialRenderProxy& material_proxy =
                    mesh_batch.material_render_proxy();
                const ShaderMapProgramRef& shader_program =
                    material_proxy.shader_program();
                const shader::ShaderGraphicsPassState* effective_state =
                    material_proxy.effective_graphics_pass_state();
                if (!shader_program || effective_state == nullptr ||
                    !shader::is_valid_shader_graphics_pass_state(
                        *effective_state))
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its active Material candidate is invalid.",
                        view_index, batch_index);
                    continue;
                }
                if (program_declares_group(
                        *shader_program, RHIBindingGroup::Global) ||
                    program_declares_group(
                        *shader_program, RHIBindingGroup::Pass))
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its Program declares Global or Pass bindings without a canonical source.",
                        view_index, batch_index);
                    continue;
                }

                auto cached_program = rhi_programs.find(shader_program.get());
                if (cached_program == rhi_programs.end())
                {
                    RHIResult<RHIShaderProgram> created_program =
                        create_rhi_shader_program(device, *shader_program);
                    if (!created_program)
                    {
                        TOY_LOG_ERROR(
                            "Forward Base Pass skipped View {} MeshBatch {} because its RHI Shader Program could not be created: {}",
                            view_index, batch_index,
                            created_program.status().message());
                        continue;
                    }
                    cached_program = rhi_programs.emplace(
                        shader_program.get(),
                        std::move(created_program).value()).first;
                }
                const RHIShaderProgram& program = cached_program->second;

                std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout>
                    vertex_layouts;
                std::vector<RHIGraphicsPipelineDesc::VertexAttribute>
                    vertex_attributes;
                std::vector<RHIVertexBufferBinding> vertex_bindings;
                RHIStatus batch_status =
                    mesh_batch.vertex_factory().build_vertex_input(
                        shader_program->data().vertex_inputs,
                        vertex_layouts,
                        vertex_attributes,
                        vertex_bindings);
                if (!batch_status)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its vertex input is incompatible: {}",
                        view_index, batch_index, batch_status.message());
                    continue;
                }

                RHIGraphicsPipelineDesc pipeline_desc;
                pipeline_desc.vertex_shader = program.vertex_shader;
                pipeline_desc.pixel_shader = program.pixel_shader;
                pipeline_desc.binding_layout = program.binding_layout;
                pipeline_desc.vertex_buffers = std::move(vertex_layouts);
                pipeline_desc.vertex_attributes = std::move(vertex_attributes);
                pipeline_desc.debug_name =
                    shader_program->data().shader_name + "/" +
                    shader_program->data().pass_name + " ForwardBasePass";
                batch_status = apply_attachment_compatibility(
                    pass_desc, pipeline_desc);
                if (!batch_status)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because attachment compatibility is invalid: {}",
                        view_index, batch_index, batch_status.message());
                    continue;
                }
                apply_graphics_pass_state(*effective_state, pipeline_desc);

                RHIResult<RHIGraphicsPipelineRef> pipeline =
                    device.create_graphics_pipeline(pipeline_desc);
                if (!pipeline)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because its pipeline could not be created: {}",
                        view_index, batch_index,
                        pipeline.status().message());
                    continue;
                }

                RHIResult<RHIBindingSetRef> view_binding =
                    materialize_view_uniform_shader_parameters(
                        device, context, program.binding_layout, *shader_program,
                        view_info.view_uniform_shader_parameters());
                if (!view_binding)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because View bindings could not be materialized: {}",
                        view_index, batch_index,
                        view_binding.status().message());
                    continue;
                }
                RHIBindingSetRef material_binding;
                if (program_declares_group(
                        *shader_program, RHIBindingGroup::Material))
                {
                    RHIResult<RHIBindingSetRef> materialized_material =
                        material_proxy.materialize(
                            device, context, program.binding_layout);
                    if (!materialized_material)
                    {
                        TOY_LOG_ERROR(
                            "Forward Base Pass skipped View {} MeshBatch {} because Material bindings could not be materialized: {}",
                            view_index, batch_index,
                            materialized_material.status().message());
                        continue;
                    }
                    material_binding =
                        std::move(materialized_material).value();
                }
                RHIResult<RHIBindingSetRef> object_binding =
                    materialize_primitive_uniform_shader_parameters(
                        device, context, program.binding_layout, *shader_program,
                        mesh_batch.scene_proxy()
                            .primitive_uniform_shader_parameters());
                if (!object_binding)
                {
                    TOY_LOG_ERROR(
                        "Forward Base Pass skipped View {} MeshBatch {} because Object bindings could not be materialized: {}",
                        view_index, batch_index,
                        object_binding.status().message());
                    continue;
                }

                RHIGraphicsBindings bindings;
                bindings.view = std::move(view_binding).value();
                bindings.material = std::move(material_binding);
                bindings.object = std::move(object_binding).value();

                RHIDrawIndexedArgs draw_args;
                draw_args.index_count = mesh_batch.index_count();
                draw_args.first_index = mesh_batch.first_index();
                prepared_viewports.push_back(viewport);
                prepared_scissors.push_back(scissor);
                prepared_pipelines.push_back(
                    std::move(pipeline).value());
                prepared_vertex_bindings.push_back(
                    std::move(vertex_bindings));
                prepared_index_bindings.push_back(
                    mesh_batch.render_data().index_buffer_binding());
                prepared_bindings.push_back(std::move(bindings));
                prepared_draw_args.push_back(draw_args);
            }
        }

        RHIStatus status = context.begin_render_pass(pass_desc);
        if (!status)
        {
            return status;
        }
        for (std::size_t draw_index = 0;
             draw_index < prepared_draw_args.size() && status;
             ++draw_index)
        {
            status = context.set_graphics_pipeline(
                prepared_pipelines[draw_index]);
            if (status)
            {
                status = context.set_viewport(
                    prepared_viewports[draw_index]);
            }
            if (status)
            {
                status = context.set_scissor(
                    prepared_scissors[draw_index]);
            }
            if (status)
            {
                status = context.set_blend_constants(
                    vec4(1.0F, 1.0F, 1.0F, 1.0F));
            }
            if (status)
            {
                status = context.set_stencil_reference(0u);
            }
            if (status)
            {
                status = context.set_vertex_buffers(
                    prepared_vertex_bindings[draw_index]);
            }
            if (status)
            {
                status = context.set_index_buffer(
                    prepared_index_bindings[draw_index]);
            }
            if (status)
            {
                status = context.bind_graphics_bindings(
                    prepared_bindings[draw_index]);
            }
            if (status)
            {
                status = context.draw_indexed(
                    prepared_draw_args[draw_index]);
            }
        }

        const RHIStatus end_status = context.end_render_pass();
        return status ? end_status : status;
    }

    void ForwardSceneRenderer::render(RenderScene& render_scene) noexcept
    {
        if (!init_views())
        {
            return;
        }
        compute_view_visibility(render_scene);
        collect_mesh_batches();
    }
}
