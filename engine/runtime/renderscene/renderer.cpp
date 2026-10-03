#include "renderscene/renderer.h"

#include <array>
#include <cassert>
#include <exception>
#include <utility>
#include <vector>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_queue.h"
#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/render_command_internal.h"
#include "rendercore/shader/global_shader_map.h"
#include "rendercore/shader/rhi_shader_program_cache.h"
#include "rendercore/shader/shader_graphics_state.h"
#include "rendercore/geometry/local_vertex_factory.h"
#include "rendercore/render_resource_manager.h"
#include "renderscene/renderer_frame.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/pass/environment_background_pass.h"
#include "renderscene/ui/imgui_renderer.h"
#include "renderscene/ui/ui_texture_registry.h"
#include "renderscene/render_scene.h"
#include "renderscene/mesh_batch.h"
#include "rendercore/material/material_render_proxy.h"
#include "rendercore/material/material.h"
#include "rendercore/scene/primitive_scene_proxy.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/viewport_output_target.h"
#include "renderscene/view/scene_renderer.h"
#include "threading/task_graph/task_graph_interface.h"

namespace toy3d
{
    namespace
    {
        RHIStatus record_hit_proxy(SceneRenderer& scene_renderer, RHIDevice& device,
                                   RHIShaderProgramCache& shader_program_cache,
                                   const ShaderMapCollection& hit_proxy_shaders, RHIGraphicsCommandContext& context,
                                   const Extent& extent, const HitProxyRequest& request,
                                   RHIReadbackRef& recorded_readback, HitProxyTable& table)
        {
            if (request.pixel_x >= extent.width || request.pixel_y >= extent.height)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument, "HitProxy pixel is outside the scene image.");
            }
            const RHIFormatCapabilities format = device.format_capabilities(PixelFormat::R32UInt);
            if (!EnumHasAnyFlags(format.usage, RHIFormatUsage::RenderTarget) ||
                !EnumHasAnyFlags(format.usage, RHIFormatUsage::CopySource))
            {
                TOY_LOG_ERROR("HitProxy R32UInt render target/readback is unsupported by this device.");
                return RHIStatus::success();
            }
            RHIResult<RHIReadbackRef> readback = device.create_readback("HitProxyPixel");
            if (!readback)
            {
                if (readback.status().code() == RHIErrorCode::Unsupported)
                {
                    TOY_LOG_ERROR("HitProxy readback is unsupported: {}", readback.status().message());
                    return RHIStatus::success();
                }
                return readback.status();
            }
            RHITextureDesc id_desc;
            id_desc.width = extent.width;
            id_desc.height = extent.height;
            id_desc.format = PixelFormat::R32UInt;
            id_desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::CopySource;
            id_desc.initial_access = RHIAccess::Common;
            id_desc.clear_value = RHIClearValue::color_value(vec4(0.0f));
            id_desc.debug_name = "HitProxyId";
            RHIResult<RHITextureRef> id_texture = device.create_texture(id_desc);
            if (!id_texture)
            {
                return id_texture.status();
            }
            RHITextureViewDesc id_view_desc;
            id_view_desc.type = RHIResourceViewType::RenderTarget;
            id_view_desc.format = PixelFormat::R32UInt;
            id_view_desc.subresources.mip_count = 1u;
            id_view_desc.subresources.layer_count = 1u;
            id_view_desc.debug_name = "HitProxyIdRTV";
            RHIResult<RHITextureViewRef> id_view = device.create_texture_view(id_texture.value(), id_view_desc);
            if (!id_view)
            {
                return id_view.status();
            }

            RHITextureDesc depth_desc;
            depth_desc.width = extent.width;
            depth_desc.height = extent.height;
            depth_desc.format = PixelFormat::D32Float;
            depth_desc.usage = RHIResourceUsage::DepthStencil;
            depth_desc.initial_access = RHIAccess::Common;
            depth_desc.clear_value = RHIClearValue::DepthZero;
            depth_desc.debug_name = "HitProxyDepth";
            RHIResult<RHITextureRef> depth_texture = device.create_texture(depth_desc);
            if (!depth_texture)
            {
                return depth_texture.status();
            }
            RHITextureViewDesc depth_view_desc;
            depth_view_desc.type = RHIResourceViewType::DepthStencil;
            depth_view_desc.format = PixelFormat::D32Float;
            depth_view_desc.subresources.aspect = RHITextureAspect::Depth;
            depth_view_desc.subresources.mip_count = 1u;
            depth_view_desc.subresources.layer_count = 1u;
            depth_view_desc.debug_name = "HitProxyDepthDSV";
            RHIResult<RHITextureViewRef> depth_view =
                device.create_texture_view(depth_texture.value(), depth_view_desc);
            if (!depth_view)
            {
                return depth_view.status();
            }

            RHIResourceTransition id_to_target;
            id_to_target.resource = id_texture.value();
            id_to_target.subresources = id_view.value()->desc().subresources;
            id_to_target.before = RHIAccess::Common;
            id_to_target.after = RHIAccess::RenderTarget;
            RHIResourceTransition depth_to_target;
            depth_to_target.resource = depth_texture.value();
            depth_to_target.subresources = depth_view.value()->desc().subresources;
            depth_to_target.before = RHIAccess::Common;
            depth_to_target.after = RHIAccess::DepthStencilWrite;
            RHIStatus status = context.transition_resources({id_to_target, depth_to_target});
            if (!status)
            {
                return status;
            }
            status = scene_renderer.render_hit_proxy(device, shader_program_cache, hit_proxy_shaders, context,
                                                     id_view.value(), depth_view.value(), table);
            if (!status)
            {
                return status;
            }
            RHIResourceTransition id_to_copy;
            id_to_copy.resource = id_texture.value();
            id_to_copy.subresources = id_view.value()->desc().subresources;
            id_to_copy.before = RHIAccess::RenderTarget;
            id_to_copy.after = RHIAccess::CopySource;
            status = context.transition_resources({id_to_copy});
            if (!status)
            {
                return status;
            }
            RHITexturePixelReadbackDesc copy;
            copy.source.texture = id_texture.value();
            copy.source.offset.x = request.pixel_x;
            copy.source.offset.y = request.pixel_y;
            copy.destination = readback.value();
            status = context.readback_texture_pixel(copy);
            if (status)
            {
                recorded_readback = std::move(readback).value();
            }
            return status;
        }
    } // namespace

    // --------------------------------------------------------------------------
    // RendererStatus: immutable Game Thread view of renderer lifecycle failures
    // --------------------------------------------------------------------------
    RendererLifecycleState RendererStatus::lifecycle_state() const noexcept
    {
        return lifecycle_state_;
    }

    bool RendererStatus::has_error() const noexcept
    {
        return error_code_ != RHIErrorCode::None;
    }

    RHIErrorCode RendererStatus::error_code() const noexcept
    {
        return error_code_;
    }

    const std::string& RendererStatus::error_message() const noexcept
    {
        return error_message_;
    }

    const std::string& RendererStatus::secondary_diagnostic() const noexcept
    {
        return secondary_diagnostic_;
    }

    // --------------------------------------------------------------------------
    // Renderer: Render Thread domain lifetime and Game Thread frame submission
    // --------------------------------------------------------------------------
    Renderer::Renderer(TaskGraphInterface& task_graph, RHISurfaceRef primary_surface,
                       RHIViewportContextDesc viewport_desc,
                       std::function<RHIResult<std::unique_ptr<RHIDevice>>()> device_factory,
                       std::shared_ptr<const GlobalShaderMap> global_shader_map,
                       std::unique_ptr<ImGuiFontAtlasData> imgui_font_atlas, bool enable_preview_scene,
                       BuiltinMeshPassPrograms mesh_pass_programs, bool enable_play_scene)
        : task_graph_(task_graph), primary_surface_input_(std::move(primary_surface)),
          viewport_desc_(std::move(viewport_desc)), device_factory_(std::move(device_factory)),
          global_shader_map_input_(std::move(global_shader_map)),
          mesh_pass_programs_input_(std::move(mesh_pass_programs)),
          imgui_font_atlas_input_(std::move(imgui_font_atlas)), enable_play_scene_(enable_play_scene),
          enable_preview_scene_(enable_preview_scene)
    {
    }

    Renderer::~Renderer()
    {
        // Engine must run logical-RT teardown before destroying the stable GT shell.
        assert(!device_);
        assert(!shader_program_cache_);
        assert(!resource_manager_);
        assert(!render_scene_);
        assert(!play_scene_);
        assert(!scene_render_targets_);
        assert(!tonemap_pass_resources_);
        assert(!imgui_renderer_);
        assert(!primary_viewport_);
        assert(!global_shader_map_input_);
        assert(!mesh_pass_programs_input_.shadow_depth_default);
    }

    ThreadStatus Renderer::initialize()
    {
        if (!is_on_logical_rendering_thread())
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidCaller,
                                         "Renderer must initialize on the logical Rendering Thread");
        }
        RendererLifecycleState expected = RendererLifecycleState::Stopped;
        if (!lifecycle_state_.compare_exchange_strong(expected, RendererLifecycleState::Starting))
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidState, "Renderer can initialize only from Stopped");
        }
        if (!primary_surface_input_ || !device_factory_ || !global_shader_map_input_)
        {
            return fail_startup(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer bootstrap requires a surface, device factory, and frozen GlobalShaderMap"));
        }

        RHIResult<std::unique_ptr<RHIDevice>> created_device = device_factory_();
        if (!created_device)
        {
            return fail_startup(created_device.status());
        }
        device_ = std::move(created_device).value();
        if (!device_)
        {
            return fail_startup(
                RHIStatus::failure(RHIErrorCode::BackendFailure, "Renderer device factory succeeded without a device"));
        }

        RHIDeviceDesc device_desc;
        device_desc.primary_surface = primary_surface_input_;
#if !defined(NDEBUG)
        // Debug builds enable the backend-independent RHI validation contract
        // so backend smoke tests exercise their diagnostic layers.
        device_desc.enable_validation = true;
#endif
        device_desc.debug_name = "RendererDevice";
        RHIStatus step_status = device_->initialize(device_desc);
        if (!step_status)
        {
            return fail_startup(step_status);
        }
        shader_program_cache_ = std::make_unique<RHIShaderProgramCache>(*device_);

        resource_manager_ = std::make_unique<RenderResourceManager>(*device_);
        render_scene_ = std::make_unique<RenderScene>(task_graph_, *resource_manager_);
        if (enable_play_scene_)
        {
            play_scene_ = std::make_unique<RenderScene>(task_graph_, *resource_manager_);
        }
        scene_render_targets_ = std::make_unique<SceneRenderTargets>();
        ui_textures_ = std::make_unique<UiTextureRegistry>();
        if (enable_preview_scene_)
        {
            preview_scene_ = std::make_unique<RenderScene>(task_graph_, *resource_manager_);
            preview_targets_ = std::make_unique<SceneRenderTargets>();
        }
        viewport_output_target_ = std::make_unique<ViewportOutputTarget>();

        if (imgui_font_atlas_input_)
        {
            imgui_renderer_ = std::make_unique<ImGuiRenderer>();
            step_status = imgui_renderer_->initialize(*device_, *shader_program_cache_, *global_shader_map_input_,
                                                      *imgui_font_atlas_input_);
            if (!step_status)
            {
                return fail_startup(step_status);
            }
        }

        tonemap_pass_resources_ = std::make_unique<TonemapPassResources>();
        step_status = tonemap_pass_resources_->initialize(*device_, *shader_program_cache_, *global_shader_map_input_);
        if (!step_status)
        {
            return fail_startup(step_status);
        }

        RHITextureDesc placeholder_desc;
        placeholder_desc.format = PixelFormat::R8G8B8A8UNorm;
        placeholder_desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
        placeholder_desc.initial_access = RHIAccess::Common;
        placeholder_desc.debug_name = "RendererWhitePlaceholder";
        RHIResult<RHITextureRef> placeholder_result = device_->create_texture(placeholder_desc);
        if (!placeholder_result)
        {
            return fail_startup(placeholder_result.status());
        }
        placeholder_texture_ = std::move(placeholder_result).value();

        RHITextureViewDesc placeholder_view_desc;
        placeholder_view_desc.type = RHIResourceViewType::ShaderResource;
        placeholder_view_desc.format = placeholder_desc.format;
        placeholder_view_desc.debug_name = "RendererWhitePlaceholderView";
        RHIResult<RHITextureViewRef> placeholder_view_result =
            device_->create_texture_view(placeholder_texture_, placeholder_view_desc);
        if (!placeholder_view_result)
        {
            return fail_startup(placeholder_view_result.status());
        }
        placeholder_texture_view_ = std::move(placeholder_view_result).value();

        RHISamplerDesc sampler_desc;
        sampler_desc.address_u = RHIAddressMode::ClampToEdge;
        sampler_desc.address_v = RHIAddressMode::ClampToEdge;
        sampler_desc.address_w = RHIAddressMode::ClampToEdge;
        sampler_desc.debug_name = "RendererPlaceholderSampler";
        RHIResult<RHISamplerRef> sampler_result = device_->create_sampler(sampler_desc);
        if (!sampler_result)
        {
            return fail_startup(sampler_result.status());
        }
        placeholder_sampler_ = std::move(sampler_result).value();

        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> context_result =
            device_->create_graphics_command_context();
        if (!context_result)
        {
            return fail_startup(context_result.status());
        }
        std::unique_ptr<RHIGraphicsCommandContext> context = std::move(context_result).value();
        if (!context)
        {
            return fail_startup(
                RHIStatus::failure(RHIErrorCode::BackendFailure, "Renderer bootstrap created no command context"));
        }

        // 这次启动引导录制只执行一次，确保 placeholder 资源在 Renderer 进入 Running 前已在 GPU 就绪。
        // 常规帧上传和绘制仍由 draw_frame() 在 viewport frame context 中录制。
        step_status = context->begin_recording("RendererBootstrap");
        if (step_status)
        {
            RHIResourceTransition placeholder_to_copy;
            placeholder_to_copy.resource = placeholder_texture_;
            placeholder_to_copy.before = RHIAccess::Common;
            placeholder_to_copy.after = RHIAccess::CopyDestination;
            step_status = context->transition_resources({placeholder_to_copy});
        }

        const std::array<std::uint8_t, 4> white_pixel = {255, 255, 255, 255};
        if (step_status)
        {
            RHITextureUploadDesc upload;
            upload.destination.texture = placeholder_texture_;
            upload.extent = {1, 1, 1};
            upload.source.data = white_pixel.data();
            upload.source.size = white_pixel.size();
            upload.source.row_pitch = white_pixel.size();
            upload.source.slice_pitch = white_pixel.size();
            step_status = context->upload_texture(upload);
        }
        if (step_status)
        {
            RHIResourceTransition placeholder_to_shader;
            placeholder_to_shader.resource = placeholder_texture_;
            placeholder_to_shader.before = RHIAccess::CopyDestination;
            placeholder_to_shader.after = RHIAccess::ShaderResourceGraphics;
            step_status = context->transition_resources({placeholder_to_shader});
        }
        if (step_status && imgui_renderer_)
        {
            step_status = imgui_renderer_->record_font_upload(*context, *imgui_font_atlas_input_);
        }

        RHICommandListRef command_list;
        if (step_status)
        {
            RHIResult<RHICommandListRef> finished = context->finish_recording();
            if (!finished)
            {
                step_status = finished.status();
            }
            else
            {
                command_list = std::move(finished).value();
                if (!command_list)
                {
                    step_status = RHIStatus::failure(RHIErrorCode::BackendFailure,
                                                     "Renderer bootstrap finished without a command list");
                }
            }
        }

        RHIQueueCompletionValue bootstrap_completion = 0;
        if (step_status)
        {
            RHISubmitInfo submit_info;
            submit_info.command_lists.push_back(std::move(command_list));
            submit_info.debug_name = "RendererBootstrap";
            RHIResult<RHISubmitResult> submitted = device_->graphics_queue().submit(submit_info);
            if (!submitted)
            {
                step_status = submitted.status();
            }
            else
            {
                bootstrap_completion = submitted.value().completion_value;
                if (bootstrap_completion == 0)
                {
                    step_status = RHIStatus::failure(RHIErrorCode::BackendFailure,
                                                     "Renderer bootstrap submit returned no completion value");
                }
            }
        }
        if (step_status)
        {
            step_status = device_->graphics_queue().wait_for_value(bootstrap_completion);
        }
        if (!step_status)
        {
            return fail_startup(step_status);
        }
        if (imgui_renderer_)
        {
            imgui_renderer_->publish_bootstrap_complete();
        }
        imgui_font_atlas_input_.reset();

        RHIResult<std::unique_ptr<RHIViewportContext>> viewport_result =
            device_->create_viewport_context(primary_surface_input_, viewport_desc_);
        if (!viewport_result)
        {
            return fail_startup(viewport_result.status());
        }
        primary_viewport_ = std::move(viewport_result).value();
        if (!primary_viewport_)
        {
            return fail_startup(
                RHIStatus::failure(RHIErrorCode::BackendFailure, "Renderer bootstrap created no primary viewport"));
        }

        published_scene_interface_.store(render_scene_.get());
        published_play_interface_.store(play_scene_.get());
        published_preview_interface_.store(preview_scene_.get());
        lifecycle_state_.store(RendererLifecycleState::Running);
        return ThreadStatus::success();
    }

    ThreadStatus Renderer::teardown()
    {
        if (!is_on_logical_rendering_thread())
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidCaller,
                                         "Renderer must teardown on the logical Rendering Thread");
        }
        const RendererLifecycleState state = lifecycle_state_.load();
        if (state == RendererLifecycleState::Stopped || state == RendererLifecycleState::Starting ||
            state == RendererLifecycleState::Stopping ||
            (state == RendererLifecycleState::Terminal && !device_ && !resource_manager_ && !render_scene_ &&
             !primary_viewport_))
        {
            return ThreadStatus::failure(ThreadErrorCode::InvalidState,
                                         "Renderer is not in a teardown-ready lifecycle state");
        }

        const bool terminal = state == RendererLifecycleState::Terminal;
        if (!terminal)
        {
            lifecycle_state_.store(RendererLifecycleState::Stopping);
        }
        published_scene_interface_.store(nullptr);
        published_preview_interface_.store(nullptr);
        release_domain(terminal);
        if (!terminal)
        {
            lifecycle_state_.store(RendererLifecycleState::Stopped);
        }
        return ThreadStatus::success();
    }

    RHIResult<RHIFrameEndResult> render_viewport_frame(
        SceneRenderer* scene_renderer, const ImGuiDrawData* ui_draw_data, const ViewportFrameOutput& output,
        RenderScene& render_scene, RHIDevice& device, RHIShaderProgramCache& shader_program_cache,
        RenderResourceManager& resource_manager, RHIViewportContext& viewport, SceneRenderTargets& scene_render_targets,
        TonemapPassResources& tonemap_pass_resources, ImGuiRenderer* imgui_renderer,
        ViewportOutputTarget& viewport_output_target, const GlobalShaderMap* global_shader_map,
        const BuiltinMeshPassPrograms& mesh_pass_programs, RHIReadbackRef* recorded_readback,
        HitProxyTable* hit_proxy_table, UiTextureRegistry* ui_textures,
        const std::function<RHIStatus(RHIGraphicsCommandContext&)>& record_ui_work)
    {
        RHIResult<std::unique_ptr<RHIFrameContext>> frame_result = viewport.begin_frame();
        if (!frame_result)
        {
            return RHIResult<RHIFrameEndResult>::failure(frame_result.status().code(), frame_result.status().message());
        }

        std::unique_ptr<RHIFrameContext> frame = std::move(frame_result).value();
        if (!frame)
        {
            return RHIResult<RHIFrameEndResult>::failure(RHIErrorCode::BackendFailure,
                                                         "Viewport begin_frame succeeded without a frame context.");
        }

        bool resource_recording_started = false;
        const auto abort_recording = [&resource_manager, &render_scene, &viewport, &frame, &resource_recording_started,
                                      imgui_renderer](const RHIStatus& failure) -> RHIResult<RHIFrameEndResult>
        {
            if (imgui_renderer != nullptr)
            {
                imgui_renderer->discard_frame_recording();
            }
            RHIStatus discard_status = RHIStatus::success();
            if (resource_recording_started)
            {
                discard_status = resource_manager.discard_recording();
                render_scene.resolve_environment_recording(false);
                if (!discard_status)
                {
                    TOY_LOG_ERROR("Renderer frame could not discard its RenderResource recording after '{}': {}",
                                  failure.message(), discard_status.message());
                }
            }

            const RHIStatus abort_status = viewport.abort_frame(std::move(frame));
            if (!abort_status)
            {
                TOY_LOG_ERROR("Renderer frame abort failed after '{}': {}", failure.message(), abort_status.message());
                return RHIResult<RHIFrameEndResult>::failure(abort_status.code(), abort_status.message());
            }
            if (!discard_status)
            {
                return RHIResult<RHIFrameEndResult>::failure(discard_status.code(), discard_status.message());
            }
            return RHIResult<RHIFrameEndResult>::failure(failure.code(), failure.message());
        };

        const Extent frame_extent = frame->extent();
        const Extent scene_extent = output.sample_in_ui ? output.scene_extent : frame_extent;
        const bool has_scene = scene_extent.width != 0u && scene_extent.height != 0u;
        if ((!output.sample_in_ui && !has_scene) || (has_scene != (scene_renderer != nullptr)) ||
            (has_scene && scene_renderer->output_extent() != scene_extent) ||
            (output.sample_in_ui && !output.texture_id.valid()))
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::OutOfDate, "Renderer frame scene output does not match its viewport request."));
        }

        RHIStatus status = RHIStatus::success();
        if (has_scene)
        {
            status = scene_render_targets.ensure_extent(device, scene_extent);
            if (!status)
            {
                return abort_recording(status);
            }
            if (output.sample_in_ui)
            {
                status = viewport_output_target.ensure_extent(device, scene_extent);
                if (!status)
                {
                    return abort_recording(status);
                }
            }
        }

        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> context_result = frame->create_graphics_command_context();
        if (!context_result)
        {
            return abort_recording(context_result.status());
        }
        std::unique_ptr<RHIGraphicsCommandContext> context = std::move(context_result).value();

        status = context->begin_recording("RendererFrame");
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

        if (has_scene)
        {
            status = scene_renderer->render_scene_passes(render_scene, device, shader_program_cache, *context,
                                                         scene_render_targets, mesh_pass_programs);
            if (!status)
            {
                return abort_recording(status);
            }
            if (output.hit_proxy_request.request_id != 0u)
            {
                if (!mesh_pass_programs.hit_proxy || recorded_readback == nullptr || hit_proxy_table == nullptr)
                {
                    return abort_recording(RHIStatus::failure(
                        RHIErrorCode::InvalidArgument, "HitProxy frame requires its ShaderMap and readback owner."));
                }
                status = record_hit_proxy(*scene_renderer, device, shader_program_cache, *mesh_pass_programs.hit_proxy,
                                          *context, scene_extent, output.hit_proxy_request, *recorded_readback,
                                          *hit_proxy_table);
                if (!status)
                {
                    return abort_recording(status);
                }
            }
        }

        std::vector<RHIResourceTransition> final_output_transitions;
        if (has_scene)
        {
            RHIResourceTransition scene_color_to_shader_resource;
            scene_color_to_shader_resource.resource = scene_render_targets.scene_color_texture();
            scene_color_to_shader_resource.subresources =
                scene_render_targets.scene_color_shader_resource_view()->desc().subresources;
            scene_color_to_shader_resource.before = RHIAccess::RenderTarget;
            scene_color_to_shader_resource.after = RHIAccess::ShaderResourceGraphics;
            final_output_transitions.push_back(std::move(scene_color_to_shader_resource));
        }
        RHIResourceTransition present_to_render_target;
        present_to_render_target.resource = frame->present_texture();
        present_to_render_target.subresources = frame->present_view()->desc().subresources;
        present_to_render_target.before = RHIAccess::Present;
        present_to_render_target.after = RHIAccess::RenderTarget;
        final_output_transitions.push_back(std::move(present_to_render_target));
        if (has_scene && output.sample_in_ui)
        {
            RHIResourceTransition viewport_to_render_target;
            viewport_to_render_target.resource = viewport_output_target.texture();
            viewport_to_render_target.subresources = viewport_output_target.render_target_view()->desc().subresources;
            viewport_to_render_target.before = viewport_output_target.access();
            viewport_to_render_target.after = RHIAccess::RenderTarget;
            final_output_transitions.push_back(std::move(viewport_to_render_target));
        }
        status = context->transition_resources(final_output_transitions);
        if (!status)
        {
            return abort_recording(status);
        }

        if (has_scene)
        {
            TonemapPassTarget tonemap_target;
            tonemap_target.color_view =
                output.sample_in_ui ? viewport_output_target.render_target_view() : frame->present_view();
            tonemap_target.extent = scene_extent;
            status =
                tonemap_pass_resources.render(device, *context, scene_render_targets.scene_color_shader_resource_view(),
                                              tonemap_target, TonemapParameters{});
            if (!status)
            {
                return abort_recording(status);
            }
            if (output.sample_in_ui)
            {
                RHIResourceTransition viewport_to_shader_resource;
                viewport_to_shader_resource.resource = viewport_output_target.texture();
                viewport_to_shader_resource.subresources =
                    viewport_output_target.shader_resource_view()->desc().subresources;
                viewport_to_shader_resource.before = RHIAccess::RenderTarget;
                viewport_to_shader_resource.after = RHIAccess::ShaderResourceGraphics;
                status = context->transition_resources({viewport_to_shader_resource});
                if (!status)
                {
                    return abort_recording(status);
                }
            }
        }

        if (record_ui_work)
        {
            status = record_ui_work(*context);
            if (!status)
            {
                return abort_recording(status);
            }
        }
        const bool has_ui = ui_draw_data != nullptr && !ui_draw_data->empty();
        if (has_ui)
        {
            if (imgui_renderer == nullptr)
            {
                return abort_recording(RHIStatus::failure(RHIErrorCode::Unsupported,
                                                          "ImGui RHI rendering is not initialized for this frame."));
            }
            ImGuiPassTarget imgui_target;
            imgui_target.color_view = frame->present_view();
            imgui_target.extent = frame_extent;
            imgui_target.load = output.sample_in_ui ? RHILoadOperation::Clear : RHILoadOperation::Load;
            imgui_target.clear_value = RHIClearValue::color_value(vec4(0.08F, 0.08F, 0.08F, 1.0F));
            status = imgui_renderer->render(
                device, *context, *ui_draw_data, imgui_target,
                has_scene && output.sample_in_ui ? viewport_output_target.shader_resource_view() : RHITextureViewRef{},
                output.texture_id, ui_textures ? ui_textures->bindings() : std::vector<ImGuiTextureBinding>{});
            if (!status)
            {
                return abort_recording(status);
            }
        }
        else if (output.sample_in_ui)
        {
            RHIRenderPassDesc clear_pass;
            RHIColorAttachmentDesc color;
            color.view = frame->present_view();
            color.load = RHILoadOperation::Clear;
            color.store = RHIStoreOperation::Store;
            color.clear_value = RHIClearValue::color_value(vec4(0.08F, 0.08F, 0.08F, 1.0F));
            clear_pass.color_attachments.push_back(std::move(color));
            clear_pass.debug_name = "EditorWindowClear";
            status = context->begin_render_pass(clear_pass);
            if (!status)
            {
                return abort_recording(status);
            }
            status = context->end_render_pass();
            if (!status)
            {
                return abort_recording(status);
            }
        }

        RHIResourceTransition render_target_to_present;
        render_target_to_present.resource = frame->present_texture();
        render_target_to_present.subresources = frame->present_view()->desc().subresources;
        render_target_to_present.before = RHIAccess::RenderTarget;
        render_target_to_present.after = RHIAccess::Present;
        status = context->transition_resources({render_target_to_present});
        if (!status)
        {
            return abort_recording(status);
        }

        RHIResult<RHICommandListRef> command_list_result = context->finish_recording();
        if (!command_list_result)
        {
            return abort_recording(command_list_result.status());
        }
        RHICommandListRef command_list = std::move(command_list_result).value();
        if (!command_list)
        {
            return abort_recording(RHIStatus::failure(RHIErrorCode::BackendFailure,
                                                      "Renderer frame finished without an immutable command list."));
        }

        RHIResult<RHIFrameEndResult> end_result = viewport.end_frame(std::move(frame), {std::move(command_list)});
        if (!end_result)
        {
            if (imgui_renderer != nullptr)
            {
                imgui_renderer->discard_frame_recording();
            }
            const RHIStatus discard_status = resource_manager.discard_recording();
            render_scene.resolve_environment_recording(false);
            if (!discard_status)
            {
                TOY_LOG_ERROR(
                    "Renderer frame submit failed and its RenderResource recording could not be discarded: {}",
                    discard_status.message());
                return RHIResult<RHIFrameEndResult>::failure(discard_status.code(), discard_status.message());
            }
            return RHIResult<RHIFrameEndResult>::failure(end_result.status().code(), end_result.status().message());
        }

        RHIFrameEndResult submitted_result = std::move(end_result).value();
        if (submitted_result.completion_value == 0u)
        {
            submitted_result.presentation_status = RHIStatus::failure(
                RHIErrorCode::BackendFailure, "Renderer frame submit returned an invalid completion value.");
        }
        if (has_ui)
        {
            const RHIStatus ui_publish_status =
                imgui_renderer->publish_frame_submission(submitted_result.completion_value);
            if (!ui_publish_status && (submitted_result.presentation_status.succeeded() ||
                                       rhi_is_recoverable_viewport_status(submitted_result.presentation_status)))
            {
                submitted_result.presentation_status = ui_publish_status;
            }
        }
        const RHIStatus commit_status = resource_manager.commit_recording();
        if (commit_status)
        {
            render_scene.resolve_environment_recording(true);
        }
        if (!commit_status)
        {
            TOY_LOG_ERROR("Renderer frame submitted but RenderResource publication failed: {}",
                          commit_status.message());
            if (submitted_result.presentation_status.succeeded() ||
                rhi_is_recoverable_viewport_status(submitted_result.presentation_status))
            {
                submitted_result.presentation_status = commit_status;
            }
        }
        if (has_scene)
        {
            scene_render_targets.publish_submitted_access(RHIAccess::ShaderResourceGraphics,
                                                          RHIAccess::DepthStencilWrite);
            if (output.sample_in_ui)
            {
                viewport_output_target.publish_submitted_access(RHIAccess::ShaderResourceGraphics);
            }
        }
        return RHIResult<RHIFrameEndResult>::success(std::move(submitted_result));
    }

    RHIStatus Renderer::record_ui_work(RHIGraphicsCommandContext& context, RHIReadbackRef& capture)
    {
        for (const UiTextureUpload& upload : pending_ui_work_.uploads)
        {
            const auto status = ui_textures_->record_upload(*device_, context, upload);
            if (!status)
            {
                return status;
            }
        }
        const PreviewFrameRequest& request = pending_ui_work_.preview;
        if (!request.request_id)
        {
            return RHIStatus::success();
        }
        if (!preview_scene_ || !preview_targets_ || !pending_preview_renderer_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Preview scene is unavailable.");
        }
        auto status = preview_targets_->ensure_extent(*device_, request.extent);
        if (!status)
        {
            return status;
        }
        status = ui_textures_->create_target(*device_, request.texture_id, request.extent);
        if (!status)
        {
            return status;
        }
        status = pending_preview_renderer_->render_scene_passes(*preview_scene_, *device_, *shader_program_cache_,
                                                                context, *preview_targets_, mesh_pass_programs_input_);
        if (!status)
        {
            return status;
        }
        if (request.show_environment)
        {
            if (!environment_background_resources_)
            {
                auto candidate = std::make_unique<EnvironmentBackgroundPassResources>();
                status = candidate->initialize(*device_, *shader_program_cache_, *global_shader_map_input_);
                if (!status)
                {
                    return status;
                }
                environment_background_resources_ = std::move(candidate);
            }
            const SceneRenderer& preview_renderer = *pending_preview_renderer_;
            for (const auto& view : preview_renderer.view_infos())
            {
                status = environment_background_resources_->render(*device_, context, *preview_scene_,
                                                                   *preview_targets_, view);
                if (!status)
                {
                    return status;
                }
            }
        }
        RHIResourceTransition color;
        color.resource = preview_targets_->scene_color_texture();
        color.before = RHIAccess::RenderTarget;
        color.after = RHIAccess::ShaderResourceGraphics;
        RHIResourceTransition output;
        output.resource = ui_textures_->texture(request.texture_id);
        output.before = RHIAccess::Common;
        output.after = RHIAccess::RenderTarget;
        status = context.transition_resources({color, output});
        if (!status)
        {
            return status;
        }
        TonemapPassTarget target;
        target.color_view = ui_textures_->target_view(request.texture_id);
        target.extent = request.extent;
        status =
            tonemap_pass_resources_->render(*device_, context, preview_targets_->scene_color_shader_resource_view(),
                                            target, TonemapParameters{request.exposure_ev});
        if (!status)
        {
            return status;
        }
        auto readback =
            device_->create_texture_readback(PixelFormat::B8G8R8A8UNorm, request.extent, "ThumbnailReadback");
        if (!readback)
        {
            return readback.status();
        }
        output.before = RHIAccess::RenderTarget;
        output.after = RHIAccess::CopySource;
        status = context.transition_resources({output});
        if (!status)
        {
            return status;
        }
        RHITextureReadbackDesc copy;
        copy.source.texture = ui_textures_->texture(request.texture_id);
        copy.extent = request.extent;
        copy.destination = readback.value();
        status = context.readback_texture(copy);
        if (!status)
        {
            return status;
        }
        output.before = RHIAccess::CopySource;
        output.after = RHIAccess::ShaderResourceGraphics;
        status = context.transition_resources({output});
        if (status)
        {
            capture = readback.value();
        }
        return status;
    }

    RHIResult<RHIFrameEndResult> Renderer::render_frame(SceneRenderer* scene_renderer,
                                                        const ImGuiDrawData* ui_draw_data,
                                                        const ViewportFrameOutput& output)
    {
        RHIReadbackRef recorded_readback;
        RHIReadbackRef ui_readback;
        RHIStatus ui_status = RHIStatus::success();
        HitProxyTable hit_proxy_table;
        RenderScene* active_scene = output.play_scene ? play_scene_.get() : render_scene_.get();
        if (!active_scene)
        {
            return RHIResult<RHIFrameEndResult>::failure(RHIErrorCode::InvalidArgument, "Play Scene is unavailable.");
        }
        auto result = render_viewport_frame(
            scene_renderer, ui_draw_data, output, *active_scene, *device_, *shader_program_cache_, *resource_manager_,
            *primary_viewport_, *scene_render_targets_, *tonemap_pass_resources_, imgui_renderer_.get(),
            *viewport_output_target_, global_shader_map_input_.get(), mesh_pass_programs_input_, &recorded_readback,
            &hit_proxy_table, ui_textures_.get(),
            [this, &ui_readback, &ui_status](RHIGraphicsCommandContext& context)
            {
                ui_status = record_ui_work(context, ui_readback);
                return ui_status;
            });
        const bool resources_submitted = result && result.value().completion_value != 0u;
        for (auto* scene : {render_scene_.get(), play_scene_.get(), preview_scene_.get()})
        {
            if (scene && scene != active_scene)
            {
                scene->resolve_environment_recording(resources_submitted);
            }
        }
        if (output.scene_feedback && scene_renderer &&
            output.scene_feedback->state.load(std::memory_order_acquire) == SceneRenderState::Pending)
        {
            const RHIStatus prepared = active_scene->preparation_status();
            if ((!result && !rhi_is_recoverable_viewport_status(result.status())) ||
                (!prepared && prepared.code() != RHIErrorCode::NotReady))
            {
                output.scene_feedback->error = !result ? result.status().message() : prepared.message();
                output.scene_feedback->state.store(SceneRenderState::Failed, std::memory_order_release);
            }
            else if (prepared && result && result.value().completion_value != 0u)
            {
                output.scene_feedback->state.store(SceneRenderState::Ready, std::memory_order_release);
            }
        }
        if (result)
        {
            if (recorded_readback)
            {
                pending_hit_readbacks_.push_back(
                    {output.hit_proxy_request, std::move(recorded_readback), std::move(hit_proxy_table)});
            }
            if (ui_readback)
            {
                const auto& preview = pending_ui_work_.preview;
                pending_ui_readbacks_.push_back(
                    {preview.request_id, preview.texture_id, preview.extent, std::move(ui_readback)});
                preview_targets_->publish_submitted_access(RHIAccess::ShaderResourceGraphics,
                                                           RHIAccess::DepthStencilWrite);
            }
            {
                std::lock_guard<std::mutex> lock(ui_results_mutex_);
                for (const auto& upload : pending_ui_work_.uploads)
                {
                    ui_results_.push_back({upload.request_id, upload.texture_id, upload.extent, {}, {}});
                }
            }
            pending_ui_work_ = {};
            pending_preview_renderer_.reset();
        }
        else
        {
            for (const auto& upload : pending_ui_work_.uploads)
            {
                ui_textures_->retire(upload.texture_id);
            }
            if (pending_ui_work_.preview.request_id)
            {
                ui_textures_->retire(pending_ui_work_.preview.texture_id);
            }
            constexpr std::uint32_t max_preview_preparation_attempts = 3;
            if (ui_status.code() == RHIErrorCode::NotReady && pending_ui_work_.preview.request_id &&
                ++pending_preview_attempts_ >= max_preview_preparation_attempts)
            {
                ui_status = RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                               "Preview mesh did not become completely drawable; retry generation.");
            }
            // Expected preview/input failures abort this frame, but do not poison the main scene domain.
            if (ui_status.code() == RHIErrorCode::Unsupported || ui_status.code() == RHIErrorCode::InvalidArgument)
            {
                std::lock_guard<std::mutex> lock(ui_results_mutex_);
                const auto& preview = pending_ui_work_.preview;
                if (preview.request_id)
                {
                    ui_results_.push_back(
                        {preview.request_id, preview.texture_id, preview.extent, {}, ui_status.message()});
                }
                for (const auto& upload : pending_ui_work_.uploads)
                {
                    ui_results_.push_back(
                        {upload.request_id, upload.texture_id, upload.extent, {}, ui_status.message()});
                }
                pending_ui_work_ = {};
                pending_preview_renderer_.reset();
                return RHIResult<RHIFrameEndResult>::failure(RHIErrorCode::NotReady, "UI image job failed.");
            }
        }
        // A bad Play candidate can be withdrawn on GT without killing the
        // author domain. Device/allocator/backend failures still remain terminal.
        if (!result && output.play_scene && output.scene_feedback &&
            output.scene_feedback->state.load(std::memory_order_acquire) == SceneRenderState::Failed &&
            (result.status().code() == RHIErrorCode::InvalidArgument ||
             result.status().code() == RHIErrorCode::Unsupported))
        {
            return RHIResult<RHIFrameEndResult>::failure(RHIErrorCode::NotReady, "Play candidate was rejected.");
        }
        return result;
    }

    void Renderer::collect_ui_readbacks()
    {
        if (!device_)
        {
            return;
        }
        const auto completed = device_->graphics_queue().completed_value();
        for (auto pending = pending_ui_readbacks_.begin(); pending != pending_ui_readbacks_.end();)
        {
            auto data = pending->readback->read_texture(completed);
            if (!data && data.status().code() == RHIErrorCode::NotReady)
            {
                ++pending;
                continue;
            }
            UiTextureResult result;
            result.request_id = pending->request_id;
            result.texture_id = pending->texture_id;
            result.extent = pending->extent;
            if (data)
            {
                result.bgra_pixels = std::move(data).value().bytes;
            }
            else
            {
                result.error = data.status().message();
            }
            {
                std::lock_guard<std::mutex> lock(ui_results_mutex_);
                ui_results_.push_back(std::move(result));
            }
            pending = pending_ui_readbacks_.erase(pending);
        }
    }

    bool Renderer::poll_ui_texture(UiTextureResult& result)
    {
        std::lock_guard<std::mutex> lock(ui_results_mutex_);
        if (ui_results_.empty())
        {
            return false;
        }
        result = std::move(ui_results_.front());
        ui_results_.pop_front();
        return true;
    }

    SceneInterface* Renderer::preview_scene_interface() const
    {
        return published_preview_interface_.load();
    }

    void Renderer::validate_material_shader_map(MaterialShaderMapValidationRef request)
    {
        if (request)
        {
            for (const auto* scene : {scene_interface(), preview_scene_interface(), play_scene_interface()})
            {
                if (scene)
                {
                    const auto generation = scene->material_usage_generation();
                    request->scene_revisions.push_back({generation, generation->load(std::memory_order_acquire)});
                }
            }
        }
        enqueue_render_command(
            "ValidateMaterialProgram",
            [this, request = std::move(request)]() noexcept
            {
                if (!request)
                {
                    return;
                }
                try
                {
                    request->status = [&]() -> RHIStatus
                    {
                        if (!device_ || !shader_program_cache_ || !request->shader_map ||
                            lifecycle_state_.load() != RendererLifecycleState::Running)
                        {
                            return RHIStatus::failure(RHIErrorCode::NotReady,
                                                      "Material validation requires a running Renderer.");
                        }
                        for (const auto& program : request->shader_map->programs())
                        {
                            if (program->data().contract.usage != shader::ShaderUsage::Material)
                            {
                                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                                          "Material validation requires a Material collection.");
                            }
                            const auto status = validate_mesh_shader(program);
                            if (!status)
                            {
                                return status;
                            }
                        }
                        for (const auto* scene : {render_scene_.get(), preview_scene_.get(), play_scene_.get()})
                        {
                            if (!scene)
                            {
                                continue;
                            }
                            std::vector<MeshBatch> batches;
                            const auto collected =
                                scene->collect_material_mesh_batches(request->shader_map->index().shader_name, batches);
                            if (!collected)
                            {
                                return collected;
                            }
                            for (const auto& batch : batches)
                            {
                                const auto& current = batch.material_render_proxy().shader_map();
                                if (!current ||
                                    current->index().shader_name != request->shader_map->index().shader_name)
                                {
                                    continue;
                                }
                                if (request->exact_targets)
                                {
                                    const auto target =
                                        std::find_if(request->targets.begin(), request->targets.end(),
                                                     [&](const MaterialShaderMapValidationTarget& value)
                                                     {
                                                         return value.proxy == &batch.material_render_proxy();
                                                     });
                                    if (target == request->targets.end() || !target->shader_map ||
                                        target->shader_map->index().source_hash !=
                                            request->shader_map->index().source_hash)
                                    {
                                        return RHIStatus::failure(
                                            RHIErrorCode::InvalidArgument,
                                            "A live Material user is not enrolled in this source candidate.");
                                    }
                                    if (target->shader_map != request->shader_map)
                                    {
                                        continue;
                                    }
                                }
                                else if (current->index().permutation_key !=
                                         request->shader_map->index().permutation_key)
                                {
                                    continue;
                                }
                                const auto forward =
                                    batch.find_program(*request->shader_map, shader::ShaderPassRole::Forward);
                                const auto context = " Actor " + std::to_string(batch.scene_proxy().actor_id()) +
                                                     ", Component " +
                                                     std::to_string(batch.scene_proxy().component_id()) + ", Section " +
                                                     std::to_string(batch.section_index());
                                if (!forward.succeeded())
                                {
                                    return RHIStatus::failure(RHIErrorCode::Unsupported, forward.error + context);
                                }
                                if (request->shader_map->requires_tangent_frame() && !batch.has_valid_tangent_frame())
                                {
                                    return RHIStatus::failure(
                                        RHIErrorCode::Unsupported,
                                        "Candidate Material requires a valid mesh tangent frame." + context);
                                }
                                MaterialDesc descriptor;
                                descriptor.shader_name = request->shader_map->index().shader_name;
                                descriptor.shader_map = request->shader_map;
                                std::string role_error;
                                if ((batch.scene_proxy().cast_shadows() &&
                                     !validate_material_mesh_pass(descriptor, shader::ShaderPassRole::ShadowDepth,
                                                                  batch.vertex_factory().type(), role_error)) ||
                                    (enable_preview_scene_ && scene != preview_scene_.get() &&
                                     !validate_material_mesh_pass(descriptor, shader::ShaderPassRole::HitProxy,
                                                                  batch.vertex_factory().type(), role_error)))
                                {
                                    return RHIStatus::failure(RHIErrorCode::Unsupported, role_error + context);
                                }
                                for (const auto& program : request->shader_map->programs())
                                {
                                    if (program->data().contract.vertex_factory != batch.vertex_factory().type())
                                    {
                                        continue;
                                    }
                                    const auto status = validate_mesh_shader(program, &batch.vertex_factory());
                                    if (!status)
                                    {
                                        return RHIStatus::failure(status.code(), program->data().pass_name + ": " +
                                                                                     status.message() + context);
                                    }
                                }
                            }
                        }
                        return RHIStatus::success();
                    }();
                }
                catch (const std::exception& error)
                {
                    request->status = RHIStatus::failure(RHIErrorCode::BackendFailure, error.what());
                }
                request->complete.store(true, std::memory_order_release);
            });
    }

    void Renderer::draw_frame(std::unique_ptr<SceneRenderer> scene_renderer,
                              std::unique_ptr<ImGuiDrawData> ui_draw_data, ViewportFrameOutput output,
                              UiRenderWork work, std::unique_ptr<SceneRenderer> preview_renderer)
    {
        enqueue_render_command(
            "DrawFrame",
            [this, scene_renderer = std::move(scene_renderer), ui_draw_data = std::move(ui_draw_data), output,
             work = std::move(work), preview_renderer = std::move(preview_renderer)]() mutable noexcept
            {
                if (lifecycle_state_.load() != RendererLifecycleState::Running || !render_scene_ ||
                    !resource_manager_ || !device_ || !primary_viewport_ || !scene_render_targets_ ||
                    !viewport_output_target_ || (!scene_renderer && !output.sample_in_ui))
                {
                    TOY_LOG_ERROR("Renderer Draw requires a complete Running domain and valid frame inputs.");
                    return;
                }
                resolve_builtin_shaders();
                collect_hit_proxy_readbacks();
                collect_ui_readbacks();

                for (auto& upload : work.uploads)
                {
                    pending_ui_work_.uploads.push_back(std::move(upload));
                }
                for (auto id : work.retire_textures)
                {
                    ui_textures_->retire(id);
                }
                if (work.preview.request_id)
                {
                    pending_preview_attempts_ = 0;
                    pending_ui_work_.preview = std::move(work.preview);
                    pending_preview_renderer_ = std::move(preview_renderer);
                }

                const Extent output_extent =
                    output.sample_in_ui ? output.window_extent : scene_renderer->output_extent();
                const RHIStatus extent_status = ensure_primary_frame_extent(output_extent);
                if (!extent_status)
                {
                    enter_terminal(extent_status);
                    scene_renderer.reset();
                    return;
                }

                RHIResult<RHIFrameEndResult> frame_result =
                    render_frame(scene_renderer.get(), ui_draw_data.get(), output);
                if (!frame_result)
                {
                    if (!rhi_is_recoverable_viewport_status(frame_result.status()))
                    {
                        enter_terminal(frame_result.status());
                    }
                }
                else
                {
                    const RHIStatus& presentation_status = frame_result.value().presentation_status;
                    if (!presentation_status.succeeded() && !rhi_is_recoverable_viewport_status(presentation_status))
                    {
                        enter_terminal(presentation_status);
                    }
                }
                scene_renderer.reset();
                collect_hit_proxy_readbacks();
                collect_ui_readbacks();
            });
    }

    void Renderer::collect_hit_proxy_readbacks()
    {
        if (!device_)
        {
            return;
        }
        const RHIQueueCompletionValue completed = device_->graphics_queue().completed_value();
        for (auto pending = pending_hit_readbacks_.begin(); pending != pending_hit_readbacks_.end();)
        {
            RHIResult<std::uint32_t> pixel = pending->readback->read_uint32(completed);
            if (!pixel && pixel.status().code() == RHIErrorCode::NotReady)
            {
                ++pending;
                continue;
            }
            if (pixel)
            {
                HitProxyTarget target;
                const HitProxyId id{pixel.value()};
                if (resolve_hit_proxy(id, pending->table, target))
                {
                    std::lock_guard<std::mutex> lock(hit_results_mutex_);
                    completed_hit_results_.push_back({pending->request, id, target});
                }
                else
                {
                    TOY_LOG_ERROR("HitProxy readback ID {} is absent from its submission table.", id.value);
                }
            }
            else
            {
                TOY_LOG_ERROR("HitProxy readback failed: {}", pixel.status().message());
            }
            pending = pending_hit_readbacks_.erase(pending);
        }
    }

    bool Renderer::poll_hit_proxy(HitProxyResult& result)
    {
        std::lock_guard<std::mutex> lock(hit_results_mutex_);
        if (completed_hit_results_.empty())
        {
            return false;
        }
        result = completed_hit_results_.front();
        completed_hit_results_.pop_front();
        return true;
    }

    RendererStatus Renderer::status() const
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        RendererStatus snapshot;
        snapshot.lifecycle_state_ = lifecycle_state_.load();
        snapshot.error_code_ = first_error_code_;
        snapshot.error_message_ = first_error_message_;
        snapshot.secondary_diagnostic_ = secondary_diagnostic_;
        return snapshot;
    }

    SceneInterface* Renderer::scene_interface() const
    {
        return published_scene_interface_.load();
    }

    SceneInterface* Renderer::play_scene_interface() const
    {
        return published_play_interface_.load();
    }

    bool Renderer::is_on_logical_rendering_thread() const
    {
        const NamedThread current_thread = task_graph_.get_current_thread_if_known();
        return current_thread != NamedThread::Unknown && current_thread == task_graph_.get_render_thread();
    }

    RHIStatus Renderer::ensure_primary_frame_extent(const Extent& extent)
    {
        if (!is_on_logical_rendering_thread())
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Renderer frame extent updates must run on the logical Rendering Thread");
        }
        if (extent.width == 0u || extent.height == 0u)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Renderer frame extent must be non-empty");
        }
        if (!device_ || !primary_viewport_)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Renderer frame extent update requires a complete domain");
        }
        if (viewport_desc_.extent == extent)
        {
            return RHIStatus::success();
        }
        const RHIStatus resize_status = primary_viewport_->request_resize(extent);
        if (resize_status)
        {
            viewport_desc_.extent = extent;
        }
        return resize_status;
    }

    ThreadStatus Renderer::fail_startup(const RHIStatus& failure)
    {
        enter_terminal(failure);
        release_domain(true);
        return ThreadStatus::failure(ThreadErrorCode::InitFailed, failure.message());
    }

    void Renderer::enter_terminal(const RHIStatus& failure) noexcept
    {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            if (first_error_code_ == RHIErrorCode::None)
            {
                first_error_code_ = failure.code();
                first_error_message_ = failure.message();
            }
            else if (!failure.succeeded())
            {
                if (!secondary_diagnostic_.empty())
                {
                    secondary_diagnostic_ += "; ";
                }
                secondary_diagnostic_ += failure.message();
            }
            // Publish Terminal only after ordinary business execution is closed,
            // so a GT status observer cannot race a newly visible terminal state
            // with a callable that is still permitted to enter Render-side state.
            render_command_detail::disable_render_command_execution(task_graph_);
            lifecycle_state_.store(RendererLifecycleState::Terminal);
        }
        if (resource_manager_)
        {
            const RHIStatus cleared = resource_manager_->clear_for_terminal();
            if (!cleared)
            {
                append_secondary_diagnostic(cleared);
            }
        }
    }

    void Renderer::append_secondary_diagnostic(const RHIStatus& failure) noexcept
    {
        if (failure.succeeded())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(status_mutex_);
        if (!secondary_diagnostic_.empty())
        {
            secondary_diagnostic_ += "; ";
        }
        secondary_diagnostic_ += failure.message();
    }

    void Renderer::release_domain(bool terminal) noexcept
    {
        published_play_interface_.store(nullptr);
        play_scene_.reset();
        if (builtin_update_)
        {
            builtin_update_->decision.store(BuiltinShaderDecision::Discard, std::memory_order_release);
            resolve_builtin_shaders();
        }

        pending_hit_readbacks_.clear();
        {
            std::lock_guard<std::mutex> lock(hit_results_mutex_);
            completed_hit_results_.clear();
        }
        published_scene_interface_.store(nullptr);
        published_preview_interface_.store(nullptr);
        pending_preview_renderer_.reset();
        pending_ui_work_ = {};
        pending_ui_readbacks_.clear();
        {
            std::lock_guard<std::mutex> lock(ui_results_mutex_);
            ui_results_.clear();
        }
        preview_scene_.reset();
        if (preview_targets_)
        {
            preview_targets_->release();
            preview_targets_.reset();
        }
        if (ui_textures_)
        {
            ui_textures_->clear();
            ui_textures_.reset();
        }
        render_scene_.reset();
        if (scene_render_targets_)
        {
            scene_render_targets_->release();
            scene_render_targets_.reset();
        }
        if (viewport_output_target_)
        {
            viewport_output_target_->release();
            viewport_output_target_.reset();
        }
        if (resource_manager_)
        {
            const RHIStatus cleared = resource_manager_->clear_for_terminal();
            if (!cleared)
            {
                append_secondary_diagnostic(cleared);
            }
            resource_manager_.reset();
        }
        if (tonemap_pass_resources_)
        {
            tonemap_pass_resources_->release();
            tonemap_pass_resources_.reset();
        }
        environment_background_resources_.reset();
        pending_environment_background_resources_.reset();
        if (imgui_renderer_)
        {
            imgui_renderer_->release();
            imgui_renderer_.reset();
        }
        imgui_font_atlas_input_.reset();
        if (shader_program_cache_)
        {
            shader_program_cache_->clear();
            shader_program_cache_.reset();
        }

        placeholder_sampler_.reset();
        placeholder_texture_view_.reset();
        placeholder_texture_.reset();

        if (device_ && !terminal)
        {
            const RHIStatus idle = device_->graphics_queue().wait_idle();
            if (!idle)
            {
                append_secondary_diagnostic(idle);
            }
        }
        primary_viewport_.reset();
        if (device_)
        {
            RHIErrorCode primary_error = RHIErrorCode::None;
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                primary_error = first_error_code_;
            }
            const RHIStatus shutdown_status = terminal && primary_error == RHIErrorCode::DeviceLost
                                                  ? device_->shutdown_after_device_lost()
                                                  : device_->shutdown();
            if (!shutdown_status)
            {
                append_secondary_diagnostic(shutdown_status);
            }
            device_.reset();
        }
        global_shader_map_input_.reset();
        mesh_pass_programs_input_ = {};
    }
} // namespace toy3d
