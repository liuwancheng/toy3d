#pragma once

#include "drivers/rhi/rhi_result.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace toy3d
{
    enum class RenderFrameCompletionState
    {
        Pending,
        Succeeded,
        Failed,
        Fatal,
        Cancelled
    };

    struct RenderFrameCompletionResult
    {
        RenderFrameCompletionState state = RenderFrameCompletionState::Pending;
        RHIErrorCode rhi_error_code = RHIErrorCode::None;
        std::string message;

        explicit operator bool() const
        {
            return state == RenderFrameCompletionState::Succeeded;
        }
    };

    class RenderFrameCompletion final
    {
    public:
        RenderFrameCompletion() = default;

        RenderFrameCompletion(const RenderFrameCompletion&) = delete;
        RenderFrameCompletion& operator=(const RenderFrameCompletion&) = delete;

        bool complete_success(
            RHIErrorCode rhi_error_code = RHIErrorCode::None);
        bool complete_failure(
            std::string message,
            RHIErrorCode rhi_error_code = RHIErrorCode::None);
        bool complete_fatal(
            std::string message,
            RHIErrorCode rhi_error_code = RHIErrorCode::None);
        bool cancel(std::string message);

        RenderFrameCompletionResult wait() const;
        RenderFrameCompletionResult result() const;
        bool is_complete() const;

    private:
        // Producer and RenderFrameDispatcher share this object; the first terminal result
        // wins so a late failure cannot rewrite an already observed frame outcome.
        bool complete(
            RenderFrameCompletionState state,
            RHIErrorCode rhi_error_code,
            std::string message);

        mutable std::mutex mutex_;
        mutable std::condition_variable completed_;
        RenderFrameCompletionResult result_;
    };

    using RenderFrameCompletionRef = std::shared_ptr<RenderFrameCompletion>;
}
