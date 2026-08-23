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

    const RHIStatus normalized_out_of_date = rhi_normalize_incomplete_acquired_frame_status(
        RHIStatus::failure(RHIErrorCode::OutOfDate, "The acquired image cannot be presented."),
        "Frame abort");
    check(normalized_out_of_date.code() == RHIErrorCode::BackendFailure,
        "an incomplete acquired frame must not remain recoverable");
    check(normalized_out_of_date.message().find("Frame abort") != std::string::npos,
        "normalized acquired-frame failure must preserve operation context");
    check(normalized_out_of_date.message().find("cannot be presented") != std::string::npos,
        "normalized acquired-frame failure must preserve the backend diagnostic");
    check(rhi_normalize_incomplete_acquired_frame_status(
        RHIStatus::failure(RHIErrorCode::NotReady, "The surface is temporarily unavailable."),
        "Frame abort").code() == RHIErrorCode::BackendFailure,
        "NotReady must become terminal after an acquired frame cannot be completed");
    check(rhi_normalize_incomplete_acquired_frame_status(
        RHIStatus::failure(RHIErrorCode::Suboptimal, "The frame could not reach presentation."),
        "Frame abort").code() == RHIErrorCode::BackendFailure,
        "Suboptimal must become terminal when an acquired frame never reached presentation");

    check(rhi_normalize_submitted_presentation_status(
        RHIStatus::success(), "Present").succeeded(),
        "successful presentation must remain successful after business submit");
    check(rhi_normalize_submitted_presentation_status(
        RHIStatus::failure(RHIErrorCode::Suboptimal, "Rebuild later."),
        "Present").code() == RHIErrorCode::Suboptimal,
        "Suboptimal must remain a recoverable presentation status after business submit");
    check(rhi_normalize_submitted_presentation_status(
        RHIStatus::failure(RHIErrorCode::OutOfDate, "Rebuild before the next frame."),
        "Present").code() == RHIErrorCode::OutOfDate,
        "OutOfDate must remain a recoverable presentation status after business submit");
    check(rhi_normalize_submitted_presentation_status(
        RHIStatus::failure(RHIErrorCode::NotReady, "Presentation did not reach a defined boundary."),
        "Present").code() == RHIErrorCode::BackendFailure,
        "NotReady must become terminal after business submit");
    check(rhi_normalize_submitted_presentation_status(
        RHIStatus::failure(RHIErrorCode::DeviceLost, "The device was lost after submit."),
        "Present").code() == RHIErrorCode::DeviceLost,
        "terminal presentation status must remain separate from successful business submit");

    const RHIStatus device_lost = RHIStatus::failure(
        RHIErrorCode::DeviceLost, "The device was lost during frame abort.");
    const RHIStatus preserved_device_lost = rhi_normalize_incomplete_acquired_frame_status(
        device_lost, "Frame abort");
    check(preserved_device_lost.code() == RHIErrorCode::DeviceLost,
        "an incomplete acquired frame must preserve an existing terminal code");
    check(preserved_device_lost.message() == device_lost.message(),
        "an incomplete acquired frame must preserve an existing terminal diagnostic");

    const RHIFrameEndResult submitted_out_of_date{
        17,
        RHIStatus::failure(RHIErrorCode::OutOfDate, "Rebuild before the next frame.")};
    check(submitted_out_of_date.completion_value == 17,
        "a submitted frame result must preserve its business completion value");
    check(submitted_out_of_date.presentation_status.code() == RHIErrorCode::OutOfDate,
        "a submitted frame result must report presentation independently from business submit");

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test(s) failed\n";
        return 1;
    }
    std::cout << "RHI viewport status tests passed\n";
    return 0;
}
