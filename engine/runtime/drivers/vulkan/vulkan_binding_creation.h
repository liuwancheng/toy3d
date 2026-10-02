#pragma once

#include "platform/platform_defines.h"

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/vulkan/vulkan_resource.h"

#if WITH_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class VulkanBindingPacket;
    class VulkanDescriptorPoolManager;

    using VulkanPhysicalBindingSources =
        std::array<std::vector<rhi_detail::ResolvedBinding>, VulkanBindingLayout::physical_set_count>;

    VulkanPhysicalBindingSources make_vulkan_physical_binding_sources(
        const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings);
    std::string make_vulkan_binding_packet_cache_key(const VulkanBindingLayout& layout, std::uint32_t physical_set,
                                                     const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings);
    RHIResult<std::vector<std::uint32_t>> collect_vulkan_dynamic_uniform_offsets(
        const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings);

    RHIResult<RHIBindingLayoutRef> create_vulkan_binding_layout(const RHIDevice& owner, VkDevice device,
                                                                const RHIBindingLayoutDesc& desc);
    RHIResult<std::shared_ptr<VulkanBindingPacket>> materialize_vulkan_binding_packet(
        const RHIDevice& owner, VkDevice device, VulkanDescriptorPoolManager& descriptor_pool_manager,
        const std::shared_ptr<VulkanBindingLayout>& layout, std::uint32_t physical_set,
        const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings);
} // namespace toy3d
