#include "drivers/rhi/rhi_viewport_context.h"

#include <iostream>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failure_count;
        }
    }
}

int main()
{
    using namespace toy3d;

    check(rhi_is_recoverable_viewport_status(RHIStatus::failure(
        RHIErrorCode::NotReady, "The viewport is minimized.")),
        "NotReady must be a recoverable viewport status");
    check(rhi_is_recoverable_viewport_status(RHIStatus::failure(
        RHIErrorCode::OutOfDate, "The presentation resources require recreation.")),
        "OutOfDate must be a recoverable viewport status");
    check(rhi_is_recoverable_viewport_status(RHIStatus::failure(
        RHIErrorCode::Suboptimal, "The completed frame used a suboptimal swapchain.")),
        "Suboptimal must be a recoverable viewport status");

    check(!rhi_is_recoverable_viewport_status(RHIStatus::success()),
        "a successful operation is not a recoverable failure status");
    check(!rhi_is_recoverable_viewport_status(RHIStatus::failure(
        RHIErrorCode::DeviceLost, "The device was lost.")),
        "DeviceLost must remain terminal to the viewport caller");
    check(!rhi_is_recoverable_viewport_status(RHIStatus::failure(
        RHIErrorCode::BackendFailure, "Presentation synchronization failed.")),
        "BackendFailure must not be silently treated as recoverable");
    check(!rhi_is_recoverable_viewport_status(RHIStatus::failure(
        RHIErrorCode::InvalidArgument, "The frame does not belong to the viewport.")),
        "caller contract violations must not be treated as recoverable");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI viewport status tests passed\n";
    return 0;
}
