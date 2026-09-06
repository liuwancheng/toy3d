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
    class VulkanDeferredDeletionQueue;
    class VulkanMemoryManager;

    RHIResult<RHIBufferRef> create_vulkan_buffer(
        const RHIDevice& owner,
        VkDevice device,
        VulkanMemoryManager& memory_manager,
        VulkanDeferredDeletionQueue& deletion_queue,
        const RHIBufferDesc& desc,
        const RHIInitialData* initial_data);
    RHIResult<RHITextureRef> create_vulkan_texture(
        const RHIDevice& owner,
        VkPhysicalDevice physical_device,
        VkDevice device,
        VulkanMemoryManager& memory_manager,
        VulkanDeferredDeletionQueue& deletion_queue,
        const RHITextureDesc& desc,
        const RHIInitialData* initial_data);
    RHIResult<RHIBufferViewRef> create_vulkan_buffer_view(
        const RHIBufferRef& buffer,
        const RHIBufferViewDesc& desc);
    RHIResult<RHITextureViewRef> create_vulkan_texture_view(
        VkDevice device,
        const RHITextureRef& texture,
        const RHITextureViewDesc& desc);
    RHIResult<RHIShaderRef> create_vulkan_shader(
        const RHIDevice& owner,
        VkDevice device,
        const RHIShaderDesc& desc);
    RHIResult<RHISamplerRef> create_vulkan_sampler(
        const RHIDevice& owner,
        VkDevice device,
        const RHISamplerDesc& desc);
    RHIResult<RHIGPUFenceRef> create_vulkan_gpu_fence(const std::string& debug_name);
}
