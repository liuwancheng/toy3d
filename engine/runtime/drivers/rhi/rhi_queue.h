#pragma once

#include "drivers/rhi/rhi_command_context.h"

#include <mutex>
#include <vector>

namespace toy3d
{
    struct RHISubmitInfo
    {
        std::vector<RHICommandListRef> command_lists;
        std::string debug_name;
    };

    struct RHISubmitResult
    {
        RHIQueueCompletionValue completion_value = 0;
    };

    class RHIQueue : public RHIResource
    {
      public:
        explicit RHIQueue(const RHIDevice& owner) : RHIResource(owner)
        {
        }
        ~RHIQueue() override = default;

        RHIQueue(const RHIQueue&) = delete;
        RHIQueue& operator=(const RHIQueue&) = delete;

        RHIResult<RHISubmitResult> submit(const RHISubmitInfo& info);

        virtual RHIQueueCompletionValue completed_value() const = 0;
        virtual RHIStatus wait_for_value(RHIQueueCompletionValue value) = 0;
        virtual RHIStatus wait_idle() = 0;

      protected:
        virtual RHIResult<RHISubmitResult> submit_impl(const RHISubmitInfo& info) = 0;

      private:
        std::mutex submission_mutex;
    };
} // namespace toy3d
