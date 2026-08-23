#include "drivers/rhi/rhi_queue.h"

#include <set>

namespace toy3d
{
    RHIResult<RHISubmitResult> RHIQueue::submit(const RHISubmitInfo& info)
    {
        std::lock_guard<std::mutex> submission_lock(submission_mutex);
        if (info.command_lists.empty())
        {
            return RHIResult<RHISubmitResult>::failure(
                RHIErrorCode::InvalidArgument,
                "Queue submission requires at least one command list.");
        }

        std::set<const RHICommandList*> unique_command_lists;
        for (const RHICommandListRef& command_list : info.command_lists)
        {
            if (!command_list || command_list->state() != RHICommandListState::Closed)
            {
                return RHIResult<RHISubmitResult>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Queue submission requires closed command lists.");
            }
            if (!unique_command_lists.emplace(command_list.get()).second)
            {
                return RHIResult<RHISubmitResult>::failure(
                    RHIErrorCode::InvalidArgument,
                    "A command list cannot appear twice in one submission.");
            }
        }
        RHIResult<RHISubmitResult> result = submit_impl(info);
        if (!result)
        {
            return result;
        }
        if (result.value().completion_value == 0)
        {
            return RHIResult<RHISubmitResult>::failure(
                RHIErrorCode::BackendFailure,
                "Backend returned an invalid queue completion value.");
        }

        for (const RHICommandListRef& command_list : info.command_lists)
        {
            // The public submission mutex preserves the Closed validation
            // through backend success, so publication cannot fail after GPU
            // work has already entered the queue.
            command_list->publish_submitted();
        }
        return result;
    }
}
