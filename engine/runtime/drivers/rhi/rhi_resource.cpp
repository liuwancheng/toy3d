#include "rhi_resource.h"

namespace toy3d
{
    RHIResult<std::uint32_t> RHIReadback::read_uint32(RHIQueueCompletionValue completed_value) const
    {
        if (readback_format() != PixelFormat::R32UInt)
        {
            return RHIResult<std::uint32_t>::failure(RHIErrorCode::InvalidArgument,
                                                     "Readback does not contain an integer pixel.");
        }
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

    RHIResult<RHITextureReadbackData> RHIReadback::read_texture(RHIQueueCompletionValue completed_value) const
    {
        const auto submitted = last_use_completion_value();
        if (!submitted || completed_value < submitted)
        {
            return RHIResult<RHITextureReadbackData>::failure(RHIErrorCode::NotReady,
                                                              "Texture readback requires a completed GPU submission.");
        }
        auto result = read_texture_impl();
        if (!result)
        {
            return result;
        }
        const auto& data = result.value();
        const std::uint64_t row_pitch = static_cast<std::uint64_t>(extent_.width) * 4;
        if (data.format != format_ || data.extent != extent_ || data.row_pitch != row_pitch ||
            data.bytes.size() != row_pitch * extent_.height)
        {
            return RHIResult<RHITextureReadbackData>::failure(
                RHIErrorCode::BackendFailure,
                "Backend color readback must return matching tightly packed image bytes.");
        }
        return result;
    }

    RHIResult<RHITextureReadbackData> RHIReadback::read_texture_impl() const
    {
        return RHIResult<RHITextureReadbackData>::failure(RHIErrorCode::Unsupported,
                                                          "This RHI backend does not support color texture readback.");
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
