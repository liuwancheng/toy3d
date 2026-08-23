#include "rendercore/frame_synchronization.h"

#include "rendercore/render_command.h"
#include "task_graph/graph_task.h"
#include "task_graph/task_graph_interface.h"

#include <exception>
#include <utility>

namespace toy3d
{
    RenderFenceWaitResult RenderFenceWaitResult::reached()
    {
        RenderFenceWaitResult result;
        result.rendering_thread_reached_ = true;
        return result;
    }

    RenderFenceWaitResult RenderFenceWaitResult::framework_failure(
        TaskGraphStatus status)
    {
        RenderFenceWaitResult result;
        result.framework_status_ = std::move(status);
        return result;
    }

    RenderFenceWaitResult RenderFenceWaitResult::renderer_terminal(
        std::string error_message)
    {
        RenderFenceWaitResult result;
        result.rendering_thread_reached_ = true;
        result.renderer_terminal_ = true;
        result.renderer_error_ = std::move(error_message);
        return result;
    }

    bool RenderFenceWaitResult::succeeded() const noexcept
    {
        return rendering_thread_reached_
            && framework_status_.succeeded()
            && !renderer_terminal_;
    }

    bool RenderFenceWaitResult::rendering_thread_reached() const noexcept
    {
        return rendering_thread_reached_;
    }

    bool RenderFenceWaitResult::has_renderer_terminal() const noexcept
    {
        return renderer_terminal_;
    }

    const TaskGraphStatus& RenderFenceWaitResult::framework_status() const noexcept
    {
        return framework_status_;
    }

    const std::string& RenderFenceWaitResult::renderer_error() const noexcept
    {
        return renderer_error_;
    }

    TaskGraphStatus RenderCommandFence::begin_fence()
    {
        TaskGraphInterface* task_graph =
            render_command_detail::get_render_command_task_graph();
        if (task_graph == nullptr)
        {
            return TaskGraphStatus::failure(
                TaskGraphErrorCode::Stopped,
                "RenderCommandFence cannot begin while the render facade is closed");
        }
        if (task_graph->get_current_thread_if_known() != NamedThread::GameThread)
        {
            return TaskGraphStatus::failure(
                TaskGraphErrorCode::InvalidCaller,
                "RenderCommandFence must begin on the GameThread");
        }
        if (completion_event_ && !completion_event_->is_complete())
        {
            return TaskGraphStatus::failure(
                TaskGraphErrorCode::InvalidState,
                "RenderCommandFence cannot begin again before its prior wait completes");
        }

        try
        {
            const NamedThread logical_render_thread = task_graph->get_render_thread();
            GraphEventRef completion = dispatch_graph_task(
                *task_graph,
                "RenderCommandFence",
                [logical_render_thread](NamedThread current_thread, const GraphEventRef&)
                {
                    if (current_thread != logical_render_thread)
                    {
                        throw TaskGraphException(TaskGraphStatus::failure(
                            TaskGraphErrorCode::InvalidCaller,
                            "RenderCommandFence executed outside the logical RenderingThread"));
                    }
                },
                NamedThread::RenderingThread,
                nullptr,
                SubsequentsMode::TrackSubsequents);
            task_graph_ = task_graph;
            completion_event_ = std::move(completion);
            return TaskGraphStatus::success();
        }
        catch (const TaskGraphException& exception)
        {
            return exception.status();
        }
        catch (const std::exception& exception)
        {
            return TaskGraphStatus::failure(
                TaskGraphErrorCode::InvalidState, exception.what());
        }
        catch (...)
        {
            return TaskGraphStatus::failure(
                TaskGraphErrorCode::InvalidState,
                "unknown exception while beginning RenderCommandFence");
        }
    }

    RenderFenceWaitResult RenderCommandFence::wait(
        const std::function<RenderFenceWaitResult()>& status_provider) const
    {
        if (!completion_event_)
        {
            RenderFenceWaitResult result = RenderFenceWaitResult::reached();
            if (status_provider)
            {
                try
                {
                    result = status_provider();
                    if (result.framework_status().succeeded()
                        && !result.has_renderer_terminal())
                    {
                        result = RenderFenceWaitResult::reached();
                    }
                }
                catch (const std::exception& exception)
                {
                    return RenderFenceWaitResult::framework_failure(
                        TaskGraphStatus::failure(
                            TaskGraphErrorCode::TaskFailed, exception.what()));
                }
                catch (...)
                {
                    return RenderFenceWaitResult::framework_failure(
                        TaskGraphStatus::failure(
                            TaskGraphErrorCode::TaskFailed,
                            "render status provider threw an unknown exception"));
                }
            }
            return result;
        }
        if (task_graph_ == nullptr)
        {
            return RenderFenceWaitResult::framework_failure(
                TaskGraphStatus::failure(
                    TaskGraphErrorCode::InvalidState,
                    "RenderCommandFence lost its Task Graph before wait"));
        }

        const TaskWaitResult waited = task_graph_->wait_until_task_completes(
            completion_event_, NamedThread::GameThread);
        if (!waited.succeeded())
        {
            return RenderFenceWaitResult::framework_failure(waited.status);
        }
        if (!status_provider)
        {
            return RenderFenceWaitResult::reached();
        }

        try
        {
            RenderFenceWaitResult result = status_provider();
            if (result.framework_status().succeeded()
                && !result.has_renderer_terminal())
            {
                return RenderFenceWaitResult::reached();
            }
            return result;
        }
        catch (const std::exception& exception)
        {
            return RenderFenceWaitResult::framework_failure(
                TaskGraphStatus::failure(
                    TaskGraphErrorCode::TaskFailed, exception.what()));
        }
        catch (...)
        {
            return RenderFenceWaitResult::framework_failure(
                TaskGraphStatus::failure(
                    TaskGraphErrorCode::TaskFailed,
                    "render status provider threw an unknown exception"));
        }
    }

    bool RenderCommandFence::is_complete() const noexcept
    {
        return !completion_event_ || completion_event_->is_complete();
    }

    FrameEndSync::FrameEndSync(
        bool allow_one_frame_thread_lag,
        std::function<RenderFenceWaitResult()> status_provider)
        : allow_one_frame_thread_lag_(allow_one_frame_thread_lag),
          status_provider_(std::move(status_provider))
    {
    }

    RenderFenceWaitResult FrameEndSync::sync_frame()
    {
        const TaskGraphStatus begun = fences_[fence_index_].begin_fence();
        if (!begun.succeeded())
        {
            return RenderFenceWaitResult::framework_failure(begun);
        }
        if (allow_one_frame_thread_lag_)
        {
            fence_index_ = (fence_index_ + 1) % fences_.size();
        }
        return fences_[fence_index_].wait(status_provider_);
    }

    RenderFenceWaitResult flush_rendering_commands(
        const std::function<RenderFenceWaitResult()>& status_provider)
    {
        RenderCommandFence fence;
        const TaskGraphStatus begun = fence.begin_fence();
        if (!begun.succeeded())
        {
            return RenderFenceWaitResult::framework_failure(begun);
        }
        return fence.wait(status_provider);
    }
}
