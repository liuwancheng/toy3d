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
        std::function<RHIResult<std::unique_ptr<RHIDevice>>()> device_factory)
        : task_graph_(task_graph),
          primary_surface_input_(std::move(primary_surface)),
          viewport_desc_(std::move(viewport_desc)),
          device_factory_(std::move(device_factory))
    {
    }

    Renderer::~Renderer()
    {
        // Engine must run logical-RT teardown before destroying the stable GT shell.
        assert(!device_);
        assert(!resource_manager_);
        assert(!render_scene_);
        assert(!scene_render_targets_);
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

        resource_manager_ = std::make_unique<RenderResourceManager>(*device_);
        render_scene_ = std::make_unique<RenderScene>(
            task_graph_, *resource_manager_);
        scene_render_targets_ = std::make_unique<SceneRenderTargets>();

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

    void Renderer::draw_scene(std::unique_ptr<SceneRenderer> scene_renderer)
    {
        enqueue_render_command(
            "DrawScene",
            [this, scene_renderer = std::move(scene_renderer)]() mutable noexcept
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

                const UIntVector2 output_size =
                    scene_renderer->view_family().output_size();
                const RHIStatus extent_status =
                    ensure_primary_frame_extent(output_size.x, output_size.y);
                if (!extent_status)
                {
                    enter_terminal(extent_status);
                    scene_renderer.reset();
                    return;
                }

                RHIResult<RHIFrameEndResult> frame_result =
                    scene_renderer->render_frame(
                        *render_scene_, *device_, *resource_manager_,
                        *primary_viewport_, *scene_render_targets_);
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
