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

namespace toy3d
{
    class VulkanDeferredDeletionQueue;
    class VulkanMemoryAllocator;
    class VulkanQueue;

    class VulkanDevice final : public RHIDevice
    {
    public:
        VulkanDevice();
        ~VulkanDevice() override;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        RHIStatus initialize(const RHIDeviceDesc& desc) override;
        RHIStatus shutdown() override;

        const RHICapabilities& capabilities() const override;
        const RHILimits& limits() const override;
        RHIFormatCapabilities format_capabilities(RHIFormat format) const override;

        RHIQueue& graphics_queue() override;

        RHIResult<std::shared_ptr<RHISwapchain>> create_swapchain(
            const RHISurfaceRef& surface,
            const RHISwapchainDesc& desc) override;

        RHIResult<std::unique_ptr<RHIViewportContext>> create_viewport_context(
            const RHISurfaceRef& surface,
            const RHIViewportContextDesc& desc) override;

        RHIResult<RHIBufferRef> create_buffer(
            const RHIBufferDesc& desc,
            const RHIInitialData* initial_data) override;

        RHIResult<RHITextureRef> create_texture(
            const RHITextureDesc& desc,
            const RHIInitialData* initial_data) override;

        RHIResult<RHIBufferViewRef> create_buffer_view(
            const RHIBufferRef& buffer,
            const RHIBufferViewDesc& desc) override;

        RHIResult<RHITextureViewRef> create_texture_view(
            const RHITextureRef& texture,
            const RHITextureViewDesc& desc) override;

        RHIResult<RHIShaderRef> create_shader(const RHIShaderDesc& desc) override;
        RHIResult<RHIBindingLayoutRef> create_binding_layout(
            const RHIBindingLayoutDesc& desc) override;
        RHIResult<RHISamplerRef> create_sampler(
            const RHISamplerDesc& desc) override;
        RHIResult<RHIBindingSetRef> create_binding_set(
            const RHIBindingSetDesc& desc) override;
        RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline(
            const RHIGraphicsPipelineDesc& desc) override;
        RHIResult<RHIGPUFenceRef> create_gpu_fence(
            const std::string& debug_name) override;

        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
            create_graphics_command_context() override;

        VkInstance instance() const;
        VkSurfaceKHR primary_surface_handle() const;
        VkPhysicalDevice physical_device() const;
        VkDevice device() const;
        VkQueue graphics_queue_handle() const;
        std::uint32_t graphics_queue_family_index() const;
        VulkanMemoryAllocator& memory_allocator();
        VulkanDeferredDeletionQueue& deferred_deletion_queue();

    private:
        RHIStatus create_instance(const RHIDeviceDesc& desc);
        RHIStatus create_debug_messenger();
        void destroy_debug_messenger();
        RHIStatus create_primary_surface(const RHISurfaceDesc& desc);
        RHIStatus select_physical_device();
        RHIStatus create_logical_device();
        void query_capabilities_and_limits();

        VkInstance vk_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT vk_debug_messenger = VK_NULL_HANDLE;
        VkSurfaceKHR primary_surface = VK_NULL_HANDLE;
        VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
        VkDevice vk_device = VK_NULL_HANDLE;
        VkQueue vk_graphics_queue = VK_NULL_HANDLE;
        std::uint32_t graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
        RHISurfaceRef primary_rhi_surface;
        RHICapabilities device_capabilities;
        RHILimits device_limits;
        std::unique_ptr<VulkanMemoryAllocator> allocator;
        std::unique_ptr<VulkanDeferredDeletionQueue> deletion_queue;
        std::unique_ptr<VulkanQueue> queue;
        bool initialized = false;
    };

    RHIResult<std::unique_ptr<RHIDevice>> create_vulkan_device();
}
