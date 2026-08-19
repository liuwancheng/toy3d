#include "renderscene/render_frame_completion.h"

#include <utility>

namespace toy3d
{
    bool RenderFrameCompletion::complete_success(RHIErrorCode rhi_error_code)
    {
        return complete(
            RenderFrameCompletionState::Succeeded,
            rhi_error_code,
            {});
    }

    bool RenderFrameCompletion::complete_failure(
        std::string message,
        RHIErrorCode rhi_error_code)
    {
        return complete(
            RenderFrameCompletionState::Failed,
            rhi_error_code,
            std::move(message));
    }

    bool RenderFrameCompletion::complete_fatal(
        std::string message,
        RHIErrorCode rhi_error_code)
    {
        return complete(
            RenderFrameCompletionState::Fatal,
            rhi_error_code,
            std::move(message));
    }

    bool RenderFrameCompletion::cancel(std::string message)
    {
        return complete(
            RenderFrameCompletionState::Cancelled,
            RHIErrorCode::None,
            std::move(message));
    }

    RenderFrameCompletionResult RenderFrameCompletion::wait() const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        completed_.wait(lock, [this]()
        {
            return result_.state != RenderFrameCompletionState::Pending;
        });
        return result_;
    }

    RenderFrameCompletionResult RenderFrameCompletion::result() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return result_;
    }

    bool RenderFrameCompletion::is_complete() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return result_.state != RenderFrameCompletionState::Pending;
    }

    bool RenderFrameCompletion::complete(
        RenderFrameCompletionState state,
        RHIErrorCode rhi_error_code,
        std::string message)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (result_.state != RenderFrameCompletionState::Pending)
            {
                return false;
            }
            result_.state = state;
            result_.rhi_error_code = rhi_error_code;
            result_.message = std::move(message);
        }
        completed_.notify_all();
        return true;
    }
}
