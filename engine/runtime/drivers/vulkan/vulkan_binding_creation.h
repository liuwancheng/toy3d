#pragma once

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_command_descriptors.h"

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
    class VulkanDescriptorPoolManager;

    RHIResult<RHIBindingLayoutRef> create_vulkan_binding_layout(const RHIDevice& owner, VkDevice device,
                                                                const RHIBindingLayoutDesc& desc);
    RHIResult<std::shared_ptr<VulkanBindingPacket>> materialize_vulkan_binding_packet(
        const RHIDevice& owner, VkDevice device, VulkanDescriptorPoolManager& descriptor_pool_manager,
        const std::shared_ptr<VulkanBindingLayout>& layout,
        std::uint32_t physical_set, const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings);
} // namespace toy3d
