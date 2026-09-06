#pragma once

#include "drivers/rhi/rhi_device.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace toy3d
{
    RHIResult<RHIGraphicsPipelineRef> create_vulkan_graphics_pipeline(
        const RHIDevice& owner,
        VkDevice device,
        const RHIGraphicsPipelineDesc& desc);
}
