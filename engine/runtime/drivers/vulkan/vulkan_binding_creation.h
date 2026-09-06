#pragma once

#include "drivers/rhi/rhi_device.h"

#if WITH_WIN64
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

namespace toy3d
{
    class VulkanBindingLayout;
    class VulkanBindingPacket;
    class VulkanBindingSet;

    RHIResult<RHIBindingLayoutRef> create_vulkan_binding_layout(const RHIDevice& owner, VkDevice device,
                                                                const RHIBindingLayoutDesc& desc);
    RHIResult<RHIBindingSetRef> create_vulkan_binding_set(const RHIBindingSetDesc& desc);
    RHIResult<std::shared_ptr<VulkanBindingPacket>> materialize_vulkan_binding_packet(
        const RHIDevice& owner, VkDevice device, const std::shared_ptr<VulkanBindingLayout>& layout,
        std::uint32_t physical_set, const std::vector<std::shared_ptr<VulkanBindingSet>>& logical_sets);
} // namespace toy3d
