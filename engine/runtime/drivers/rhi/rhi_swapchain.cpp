#include "drivers/rhi/rhi_swapchain.h"

namespace toy3d
{
    RHIResult<RHIAcquiredImage> RHISwapchain::acquire_next_image()
    {
        RHIResult<RHIAcquiredImage> result = acquire_next_image_impl();
        if (!result)
        {
            return result;
        }

        const RHIAcquiredImage& image = result.value();
        if (image.image_index >= swapchain_desc.image_count ||
            !image.back_buffer ||
            !image.available_token)
        {
            return RHIResult<RHIAcquiredImage>::failure(
                RHIErrorCode::BackendFailure,
                "Backend returned an invalid acquired swapchain image.");
        }
        return result;
    }

    RHIStatus RHISwapchain::present(RHIQueue& queue, const RHIPresentInfo& info)
    {
        const RHIStatus validation = validate_present_info(swapchain_desc, info);
        if (!validation)
        {
            return validation;
        }
        return present_impl(queue, info);
    }

    RHIStatus RHISwapchain::resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "Swapchain extent must be non-zero.");
        }

        const RHIStatus status = resize_impl(width, height);
        if (status)
        {
            update_desc_after_resize(width, height);
        }
        return status;
    }

    RHIStatus validate_swapchain_desc(const RHISwapchainDesc& desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Swapchain extent must be non-zero.");
        }
        if (desc.image_count < 2)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Swapchain requires at least two images.");
        }
        if (desc.format == RHIFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Swapchain format must be specified.");
        }
        return RHIStatus::success();
    }

    RHIStatus validate_present_info(
        const RHISwapchainDesc& swapchain_desc,
        const RHIPresentInfo& info)
    {
        if (info.image_index >= swapchain_desc.image_count)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Presented image index is outside the swapchain.");
        }
        if (!info.wait_token)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Presentation requires a completion token.");
        }
        return RHIStatus::success();
    }
}
