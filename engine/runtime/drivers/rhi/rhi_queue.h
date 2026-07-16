#pragma once

#include "drivers/rhi/rhi_command_context.h"

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
        RHISubmitSerial serial = 0;
    };

    class RHIQueue
    {
    public:
        RHIQueue() = default;
        virtual ~RHIQueue() = default;

        RHIQueue(const RHIQueue&) = delete;
        RHIQueue& operator=(const RHIQueue&) = delete;

        RHIResult<RHISubmitResult> submit(const RHISubmitInfo& info);

        virtual RHISubmitSerial completed_serial() const = 0;
        virtual RHIStatus wait(RHISubmitSerial serial) = 0;
        virtual RHIStatus wait_idle() = 0;

    protected:
        virtual RHIResult<RHISubmitResult> submit_impl(
            const RHISubmitInfo& info) = 0;
    };
}
