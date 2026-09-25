#include "rhi_resource.h"


namespace toy3d
{
    RHIResult<std::uint32_t> RHIReadback::read_uint32(RHIQueueCompletionValue completed_value) const
    {
        const RHIQueueCompletionValue submitted = last_use_completion_value();
        if (submitted == 0 || completed_value < submitted)
        {
            return RHIResult<std::uint32_t>::failure(RHIErrorCode::NotReady,
                                                     "Readback requires a completed GPU submission.");
        }
        return read_uint32_impl();
    }

    void RHIReadback::mark_used(RHIQueueCompletionValue completion_value)
    {
        RHIQueueCompletionValue previous = last_use_value.load();
        while (previous < completion_value && !last_use_value.compare_exchange_weak(previous, completion_value))
        {
        }
    }

    RHIQueueCompletionValue RHIReadback::last_use_completion_value() const
    {
        return last_use_value.load();
    }

    RHIResult<std::uint32_t> RHIReadback::read_uint32_impl() const
    {
        return RHIResult<std::uint32_t>::failure(RHIErrorCode::Unsupported,
                                                 "This RHI backend does not support pixel readback.");
    }

    // Public RHI resources are intentionally data-only identities. Native
    // destruction is owned by backend subclasses and deferred by queue completion value.

    RHIStatus validate_surface_desc(const RHISurfaceDesc& desc)
    {
        if (desc.platform == RHISurfacePlatform::Unknown || desc.window_handle == nullptr)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "Surface requires a supported platform and window handle.");
        }
        if (desc.platform == RHISurfacePlatform::Win32 && desc.application_handle == nullptr)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Win32 surface requires an application handle.");
        }
        return RHIStatus::success();
    }
} // namespace toy3d
