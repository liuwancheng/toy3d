#include "renderscene/render_frame_completion.h"

#include <utility>

namespace toy3d
{
    bool RenderFrameCompletion::complete_success()
    {
        return complete(RenderFrameCompletionState::Succeeded, {});
    }

    bool RenderFrameCompletion::complete_failure(std::string message)
    {
        return complete(RenderFrameCompletionState::Failed, std::move(message));
    }

    bool RenderFrameCompletion::cancel(std::string message)
    {
        return complete(RenderFrameCompletionState::Cancelled, std::move(message));
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
        std::string message)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (result_.state != RenderFrameCompletionState::Pending)
            {
                return false;
            }
            result_.state = state;
            result_.message = std::move(message);
        }
        completed_.notify_all();
        return true;
    }
}
