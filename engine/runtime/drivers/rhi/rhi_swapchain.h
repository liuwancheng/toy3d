#pragma once

#include "drivers/rhi/rhi_queue.h"

namespace toy3d
{
    struct RHISwapchainDesc
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t image_count = 2;
        RHIFormat format = RHIFormat::B8G8R8A8UNorm;
        RHIPresentMode present_mode = RHIPresentMode::Fifo;
        std::string debug_name;
    };

    struct RHIAcquiredImage
    {
        std::uint32_t image_index = 0;
        RHITextureRef back_buffer;
        RHISyncTokenRef available_token;
        bool suboptimal = false;
    };

    struct RHIPresentInfo
    {
        std::uint32_t image_index = 0;
        RHISyncTokenRef wait_token;
    };

    class RHISwapchain : public RHIObject
    {
    public:
        explicit RHISwapchain(RHISwapchainDesc desc)
            : RHIObject(desc.debug_name)
            , swapchain_desc(std::move(desc))
        {
        }

        ~RHISwapchain() override = default;

        const RHISwapchainDesc& desc() const
        {
            return swapchain_desc;
        }

        RHIResult<RHIAcquiredImage> acquire_next_image();
        RHIStatus present(
            RHIQueue& queue,
            const RHIPresentInfo& info);
        RHIStatus resize(std::uint32_t width, std::uint32_t height);
        virtual RHITextureRef back_buffer(std::uint32_t image_index) const = 0;

    protected:
        virtual RHIResult<RHIAcquiredImage> acquire_next_image_impl() = 0;
        virtual RHIStatus present_impl(
            RHIQueue& queue,
            const RHIPresentInfo& info) = 0;
        virtual RHIStatus resize_impl(std::uint32_t width, std::uint32_t height) = 0;

        void update_desc_after_resize(std::uint32_t width, std::uint32_t height)
        {
            swapchain_desc.width = width;
            swapchain_desc.height = height;
        }

    private:
        RHISwapchainDesc swapchain_desc;
    };

    using RHISwapchainRef = std::shared_ptr<RHISwapchain>;

    RHIStatus validate_swapchain_desc(const RHISwapchainDesc& desc);
    RHIStatus validate_present_info(
        const RHISwapchainDesc& swapchain_desc,
        const RHIPresentInfo& info);
}
