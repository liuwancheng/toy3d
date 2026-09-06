#include "renderscene/renderer.h"

#include <array>
#include <cassert>
#include <utility>

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_queue.h"
#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/render_command_internal.h"
#include "renderscene/render_resource_manager.h"
#include "renderscene/renderer_frame.h"
#include "renderscene/postprocess/tonemap_pass.h"
#include "renderscene/ui/imgui_renderer.h"
#include "renderscene/render_scene.h"
#include "renderscene/scene_render_targets.h"
#include "renderscene/view/scene_renderer.h"
#include "task_graph/task_graph_interface.h"

namespace toy3d
{
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

    Renderer::Renderer(
        TaskGraphInterface& task_graph,
        RHISurfaceRef primary_surface,
        RHIViewportContextDesc viewport_desc,
        std::function<RHIResult<std::unique_ptr<RHIDevice>>()> device_factory,
        std::shared_ptr<const ShaderMapProgram> tonemap_program,
        std::shared_ptr<const ShaderMapProgram> imgui_program,
        std::unique_ptr<ImGuiFontAtlasData> imgui_font_atlas)
        : task_graph_(task_graph),
          primary_surface_input_(std::move(primary_surface)),
          viewport_desc_(std::move(viewport_desc)),
          device_factory_(std::move(device_factory)),
          tonemap_program_input_(std::move(tonemap_program)),
          imgui_program_input_(std::move(imgui_program)),
          imgui_font_atlas_input_(std::move(imgui_font_atlas))
    {
    }

    Renderer::~Renderer()
    {
        // Engine must run logical-RT teardown before destroying the stable GT shell.
        assert(!device_);
        assert(!resource_manager_);
        assert(!render_scene_);
        assert(!scene_render_targets_);
        assert(!tonemap_pass_resources_);
        assert(!imgui_renderer_);
        assert(!primary_viewport_);
    }

    ThreadStatus Renderer::initialize()
    {
        if (!is_on_logical_rendering_thread())
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidCaller,
                "Renderer must initialize on the logical Rendering Thread");
        }
        RendererLifecycleState expected = RendererLifecycleState::Stopped;
        if (!lifecycle_state_.compare_exchange_strong(
                expected, RendererLifecycleState::Starting))
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidState,
                "Renderer can initialize only from Stopped");
        }
        if (!primary_surface_input_ || !device_factory_)
        {
            return fail_startup(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer bootstrap requires a surface and device factory"));
        }

        RHIResult<std::unique_ptr<RHIDevice>> created_device = device_factory_();
        if (!created_device)
        {
            return fail_startup(created_device.status());
        }
        device_ = std::move(created_device).value();
        if (!device_)
        {
            return fail_startup(RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Renderer device factory succeeded without a device"));
        }

        RHIDeviceDesc device_desc;
        device_desc.primary_surface = primary_surface_input_;
        device_desc.debug_name = "RendererDevice";
        RHIStatus step_status = device_->initialize(device_desc);
        if (!step_status)
        {
            return fail_startup(step_status);
        }
        if (imgui_font_atlas_input_)
        {
            if (!imgui_program_input_)
            {
                return fail_startup(RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Renderer ImGui bootstrap requires its ShaderMap Program"));
            }
            imgui_renderer_ = std::make_unique<ImGuiRenderer>();
            step_status = imgui_renderer_->initialize(
                *device_, *imgui_program_input_, *imgui_font_atlas_input_);
            if (!step_status)
            {
                return fail_startup(step_status);
            }
        }

        resource_manager_ = std::make_unique<RenderResourceManager>(*device_);
        render_scene_ = std::make_unique<RenderScene>(
            task_graph_, *resource_manager_);
        scene_render_targets_ = std::make_unique<SceneRenderTargets>();
        if (!tonemap_program_input_)
        {
            return fail_startup(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer bootstrap requires the Tonemap ShaderMap Program"));
        }
        tonemap_pass_resources_ = std::make_unique<TonemapPassResources>();
        step_status = tonemap_pass_resources_->initialize(
            *device_, *tonemap_program_input_);
        if (!step_status)
        {
            return fail_startup(step_status);
        }

        RHITextureDesc placeholder_desc;
        placeholder_desc.format = PixelFormat::R8G8B8A8UNorm;
        placeholder_desc.usage =
            RHIResourceUsage::ShaderResource |
            RHIResourceUsage::CopyDestination;
        placeholder_desc.initial_access = RHIAccess::Common;
        placeholder_desc.debug_name = "RendererWhitePlaceholder";
        RHIResult<RHITextureRef> placeholder_result =
            device_->create_texture(placeholder_desc);
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
            device_->create_texture_view(
                placeholder_texture_, placeholder_view_desc);
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
        RHIResult<RHISamplerRef> sampler_result =
            device_->create_sampler(sampler_desc);
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
        std::unique_ptr<RHIGraphicsCommandContext> context =
            std::move(context_result).value();
        if (!context)
        {
            return fail_startup(RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Renderer bootstrap created no command context"));
        }

        step_status = context->begin_recording("RendererBootstrap");
        if (step_status)
        {
            RHIResourceTransition placeholder_to_copy;
            placeholder_to_copy.resource = placeholder_texture_;
            placeholder_to_copy.before = RHIAccess::Common;
            placeholder_to_copy.after = RHIAccess::CopyDestination;
            step_status = context->transition_resources(
                {placeholder_to_copy});
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
            step_status = imgui_renderer_->record_font_upload(
                *context, *imgui_font_atlas_input_);
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
                    step_status = RHIStatus::failure(
                        RHIErrorCode::BackendFailure,
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
            RHIResult<RHISubmitResult> submitted =
                device_->graphics_queue().submit(submit_info);
            if (!submitted)
            {
                step_status = submitted.status();
            }
            else
            {
                bootstrap_completion = submitted.value().completion_value;
                if (bootstrap_completion == 0)
                {
                    step_status = RHIStatus::failure(
                        RHIErrorCode::BackendFailure,
                        "Renderer bootstrap submit returned no completion value");
                }
            }
        }
        if (step_status)
        {
            step_status =
                device_->graphics_queue().wait_for_value(bootstrap_completion);
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
            return fail_startup(RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Renderer bootstrap created no primary viewport"));
        }

        published_scene_interface_.store(render_scene_.get());
        lifecycle_state_.store(RendererLifecycleState::Running);
        return ThreadStatus::success();
    }

    ThreadStatus Renderer::teardown()
    {
        if (!is_on_logical_rendering_thread())
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidCaller,
                "Renderer must teardown on the logical Rendering Thread");
        }
        const RendererLifecycleState state = lifecycle_state_.load();
        if (state == RendererLifecycleState::Stopped ||
            state == RendererLifecycleState::Starting ||
            state == RendererLifecycleState::Stopping ||
            (state == RendererLifecycleState::Terminal && !device_ &&
                !resource_manager_ && !render_scene_ && !primary_viewport_))
        {
            return ThreadStatus::failure(
                ThreadErrorCode::InvalidState,
                "Renderer is not in a teardown-ready lifecycle state");
        }

        const bool terminal = state == RendererLifecycleState::Terminal;
        if (!terminal)
        {
            lifecycle_state_.store(RendererLifecycleState::Stopping);
        }
        published_scene_interface_.store(nullptr);
        release_domain(terminal);
        if (!terminal)
        {
            lifecycle_state_.store(RendererLifecycleState::Stopped);
        }
        return ThreadStatus::success();
    }

    RHIResult<RHIFrameEndResult> render_viewport_frame(
        SceneRenderer& scene_renderer,
        const ImGuiDrawData* ui_draw_data,
        RenderScene& render_scene,
        RHIDevice& device,
        RenderResourceManager& resource_manager,
        RHIViewportContext& viewport,
        SceneRenderTargets& scene_render_targets,
        TonemapPassResources& tonemap_pass_resources,
        ImGuiRenderer* imgui_renderer)
    {
        RHIResult<std::unique_ptr<RHIFrameContext>> frame_result =
            viewport.begin_frame();
        if (!frame_result)
        {
            return RHIResult<RHIFrameEndResult>::failure(
                frame_result.status().code(), frame_result.status().message());
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
             &resource_recording_started, imgui_renderer](
                const RHIStatus& failure) -> RHIResult<RHIFrameEndResult>
            {
                if (imgui_renderer != nullptr)
                {
                    imgui_renderer->discard_frame_recording();
                }
                RHIStatus discard_status = RHIStatus::success();
                if (resource_recording_started)
                {
                    discard_status = resource_manager.discard_recording();
                    if (!discard_status)
                    {
                        TOY_LOG_ERROR(
                            "Renderer frame could not discard its RenderResource recording after '{}': {}",
                            failure.message(), discard_status.message());
                    }
                }

                const RHIStatus abort_status =
                    viewport.abort_frame(std::move(frame));
                if (!abort_status)
                {
                    TOY_LOG_ERROR(
                        "Renderer frame abort failed after '{}': {}",
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
                "Renderer frame requires present attachments."));
        }
        if (!frame->present_texture()->is_owned_by(device) ||
            !frame->present_view()->is_owned_by(device) ||
            frame->present_view()->texture() != frame->present_texture())
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer frame present attachments must belong to its device and current frame."));
        }
        if (scene_renderer.output_size() !=
            UIntVector2(frame->width(), frame->height()))
        {
            return abort_recording(RHIStatus::failure(
                RHIErrorCode::OutOfDate,
                "Renderer frame View family output does not match the acquired frame extent."));
        }

        RHIStatus status = scene_render_targets.ensure_extent(
            device, frame->width(), frame->height());
        if (!status)
        {
            return abort_recording(status);
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

        status = scene_renderer.render_scene_passes(
            render_scene, device, *context, scene_render_targets);
        if (!status)
        {
            return abort_recording(status);
        }

        RHIResourceTransition scene_color_to_shader_resource;
        scene_color_to_shader_resource.resource =
            scene_render_targets.scene_color_texture();
        scene_color_to_shader_resource.subresources =
            scene_render_targets.scene_color_shader_resource_view()
                ->desc().subresources;
        scene_color_to_shader_resource.before = RHIAccess::RenderTarget;
        scene_color_to_shader_resource.after =
            RHIAccess::ShaderResourceGraphics;
        RHIResourceTransition present_to_render_target;
        present_to_render_target.resource = frame->present_texture();
        present_to_render_target.subresources =
            frame->present_view()->desc().subresources;
        present_to_render_target.before = RHIAccess::Present;
        present_to_render_target.after = RHIAccess::RenderTarget;
        status = context->transition_resources(
            {scene_color_to_shader_resource, present_to_render_target});
        if (!status)
        {
            return abort_recording(status);
        }

        TonemapPassTarget tonemap_target;
        tonemap_target.color_view = frame->present_view();
        tonemap_target.width = frame->width();
        tonemap_target.height = frame->height();
        status = tonemap_pass_resources.render(
            device, *context,
            scene_render_targets.scene_color_shader_resource_view(),
            tonemap_target, TonemapParameters{});
        if (!status)
        {
            return abort_recording(status);
        }

        const bool has_ui = ui_draw_data != nullptr && !ui_draw_data->empty();
        if (has_ui)
        {
            if (imgui_renderer == nullptr)
            {
                return abort_recording(RHIStatus::failure(
                    RHIErrorCode::Unsupported,
                    "ImGui RHI rendering is not initialized for this frame."));
            }
            ImGuiPassTarget imgui_target;
            imgui_target.color_view = frame->present_view();
            imgui_target.width = frame->width();
            imgui_target.height = frame->height();
            imgui_target.load = RHILoadOperation::Load;
            status = imgui_renderer->render(
                device, *context, *ui_draw_data, imgui_target);
            if (!status)
            {
                return abort_recording(status);
            }
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
                "Renderer frame finished without an immutable command list."));
        }

        RHIResult<RHIFrameEndResult> end_result = viewport.end_frame(
            std::move(frame), {std::move(command_list)});
        if (!end_result)
        {
            if (imgui_renderer != nullptr)
            {
                imgui_renderer->discard_frame_recording();
            }
            const RHIStatus discard_status =
                resource_manager.discard_recording();
            if (!discard_status)
            {
                TOY_LOG_ERROR(
                    "Renderer frame submit failed and its RenderResource recording could not be discarded: {}",
                    discard_status.message());
                return RHIResult<RHIFrameEndResult>::failure(
                    discard_status.code(), discard_status.message());
            }
            return RHIResult<RHIFrameEndResult>::failure(
                end_result.status().code(), end_result.status().message());
        }

        RHIFrameEndResult submitted_result = std::move(end_result).value();
        if (submitted_result.completion_value == 0u)
        {
            submitted_result.presentation_status = RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "Renderer frame submit returned an invalid completion value.");
        }
        if (has_ui)
        {
            const RHIStatus ui_publish_status =
                imgui_renderer->publish_frame_submission(
                    submitted_result.completion_value);
            if (!ui_publish_status &&
                (submitted_result.presentation_status.succeeded() ||
                 rhi_is_recoverable_viewport_status(
                     submitted_result.presentation_status)))
            {
                submitted_result.presentation_status = ui_publish_status;
            }
        }
        const RHIStatus commit_status = resource_manager.commit_recording();
        if (!commit_status)
        {
            TOY_LOG_ERROR(
                "Renderer frame submitted but RenderResource publication failed: {}",
                commit_status.message());
            if (submitted_result.presentation_status.succeeded() ||
                rhi_is_recoverable_viewport_status(
                    submitted_result.presentation_status))
            {
                submitted_result.presentation_status = commit_status;
            }
        }
        scene_render_targets.publish_submitted_access(
            RHIAccess::ShaderResourceGraphics,
            RHIAccess::DepthStencilWrite);
        return RHIResult<RHIFrameEndResult>::success(
            std::move(submitted_result));
    }

    RHIResult<RHIFrameEndResult> Renderer::render_frame(
        SceneRenderer& scene_renderer,
        const ImGuiDrawData* ui_draw_data)
    {
        return render_viewport_frame(
            scene_renderer,
            ui_draw_data,
            *render_scene_,
            *device_,
            *resource_manager_,
            *primary_viewport_,
            *scene_render_targets_,
            *tonemap_pass_resources_,
            imgui_renderer_.get());
    }

    void Renderer::draw_frame(
        std::unique_ptr<SceneRenderer> scene_renderer,
        std::unique_ptr<ImGuiDrawData> ui_draw_data)
    {
        enqueue_render_command(
            "DrawFrame",
            [this,
             scene_renderer = std::move(scene_renderer),
             ui_draw_data = std::move(ui_draw_data)]() mutable noexcept
            {
                if (lifecycle_state_.load() != RendererLifecycleState::Running ||
                    !render_scene_ || !resource_manager_ || !device_ ||
                    !primary_viewport_ || !scene_render_targets_ ||
                    !scene_renderer)
                {
                    TOY_LOG_ERROR(
                        "Renderer Draw requires a complete Running domain and a SceneRenderer.");
                    return;
                }

                const UIntVector2 output_size = scene_renderer->output_size();
                const RHIStatus extent_status =
                    ensure_primary_frame_extent(output_size.x, output_size.y);
                if (!extent_status)
                {
                    enter_terminal(extent_status);
                    scene_renderer.reset();
                    return;
                }

                RHIResult<RHIFrameEndResult> frame_result =
                    render_frame(*scene_renderer, ui_draw_data.get());
                if (!frame_result)
                {
                    if (!rhi_is_recoverable_viewport_status(frame_result.status()))
                    {
                        enter_terminal(frame_result.status());
                    }
                }
                else
                {
                    const RHIStatus& presentation_status =
                        frame_result.value().presentation_status;
                    if (!presentation_status.succeeded() &&
                        !rhi_is_recoverable_viewport_status(presentation_status))
                    {
                        enter_terminal(presentation_status);
                    }
                }
                scene_renderer.reset();
            });
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

    bool Renderer::is_on_logical_rendering_thread() const
    {
        const NamedThread current_thread = task_graph_.get_current_thread_if_known();
        return current_thread != NamedThread::Unknown &&
            current_thread == task_graph_.get_render_thread();
    }

    RHIStatus Renderer::ensure_primary_frame_extent(
        std::uint32_t width,
        std::uint32_t height)
    {
        if (!is_on_logical_rendering_thread())
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer frame extent updates must run on the logical Rendering Thread");
        }
        if (width == 0u || height == 0u)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer frame extent must be non-empty");
        }
        if (!device_ || !primary_viewport_)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Renderer frame extent update requires a complete domain");
        }
        if (viewport_desc_.width == width && viewport_desc_.height == height)
        {
            return RHIStatus::success();
        }
        const RHIStatus resize_status =
            primary_viewport_->request_resize(width, height);
        if (resize_status)
        {
            viewport_desc_.width = width;
            viewport_desc_.height = height;
        }
        return resize_status;
    }

    ThreadStatus Renderer::fail_startup(const RHIStatus& failure)
    {
        enter_terminal(failure);
        release_domain(true);
        return ThreadStatus::failure(
            ThreadErrorCode::InitFailed, failure.message());
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
        published_scene_interface_.store(nullptr);
        render_scene_.reset();
        if (scene_render_targets_)
        {
            scene_render_targets_->release();
            scene_render_targets_.reset();
        }
        if (tonemap_pass_resources_)
        {
            tonemap_pass_resources_->release();
            tonemap_pass_resources_.reset();
        }
        if (imgui_renderer_)
        {
            imgui_renderer_->release();
            imgui_renderer_.reset();
        }
        imgui_font_atlas_input_.reset();
        if (resource_manager_)
        {
            const RHIStatus cleared = resource_manager_->clear_for_terminal();
            if (!cleared)
            {
                append_secondary_diagnostic(cleared);
            }
            resource_manager_.reset();
        }

        if (device_ && !terminal)
        {
            const RHIStatus idle = device_->graphics_queue().wait_idle();
            if (!idle)
            {
                append_secondary_diagnostic(idle);
            }
        }
        primary_viewport_.reset();
        placeholder_sampler_.reset();
        placeholder_texture_view_.reset();
        placeholder_texture_.reset();
        if (device_)
        {
            RHIErrorCode primary_error = RHIErrorCode::None;
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                primary_error = first_error_code_;
            }
            const RHIStatus shutdown_status =
                terminal && primary_error == RHIErrorCode::DeviceLost
                ? device_->shutdown_after_device_lost()
                : device_->shutdown();
            if (!shutdown_status)
            {
                append_secondary_diagnostic(shutdown_status);
            }
            device_.reset();
        }
    }
}
