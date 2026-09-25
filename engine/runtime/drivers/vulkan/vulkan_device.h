#pragma once

#include "drivers/rhi/rhi_device.h"
#include "drivers/vulkan/vulkan_memory_manager.h"
#include "drivers/vulkan/vulkan_upload_manager.h"
#include "drivers/vulkan/vulkan_descriptor_pool_manager.h"

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
    class VulkanQueue;

    struct VulkanDeviceObservation
    {
        VulkanMemoryManagerStats memory;
        VulkanUploadManagerStats upload;
        VulkanDescriptorPoolManagerStats descriptors;
        std::size_t pending_deletions = 0;
        RHIQueueCompletionValue completed_value = 0;
    };

    class VulkanDevice final : public RHIDevice
    {
      public:
        VulkanDevice();
        ~VulkanDevice() override;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        RHIStatus initialize(const RHIDeviceDesc& desc) override;

        const RHICapabilities& capabilities() const override;
        const RHILimits& limits() const override;
        RHIFormatCapabilities format_capabilities(PixelFormat format) const override;

        RHIQueue& graphics_queue() override;

        VulkanDeviceObservation observation_snapshot() const;

      protected:
        RHIResult<std::unique_ptr<RHIViewportContext>> create_viewport_context_impl(
            const RHISurfaceRef& surface, const RHIViewportContextDesc& desc) override;
        RHIResult<RHIBufferRef> create_buffer_impl(const RHIBufferDesc& desc,
                                                   const RHIInitialData* initial_data) override;
        RHIResult<RHITextureRef> create_texture_impl(const RHITextureDesc& desc,
                                                     const RHIInitialData* initial_data) override;
        RHIResult<RHIReadbackRef> create_readback_impl(const std::string& debug_name) override;
        RHIResult<RHIBufferViewRef> create_buffer_view_impl(const RHIBufferRef& buffer,
                                                            const RHIBufferViewDesc& desc) override;
        RHIResult<RHITextureViewRef> create_texture_view_impl(const RHITextureRef& texture,
                                                              const RHITextureViewDesc& desc) override;
        RHIResult<RHIShaderRef> create_shader_impl(const RHIShaderDesc& desc) override;
        RHIResult<RHIBindingLayoutRef> create_binding_layout_impl(const RHIBindingLayoutDesc& desc) override;
        RHIResult<RHISamplerRef> create_sampler_impl(const RHISamplerDesc& desc) override;
        RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline_impl(const RHIGraphicsPipelineDesc& desc) override;
        RHIResult<RHIGPUFenceRef> create_gpu_fence_impl(const std::string& debug_name) override;
        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context_impl() override;
        bool is_initialized_impl() const override;
        RHIStatus wait_idle_before_shutdown_impl() override;
        RHIStatus shutdown_impl() override;

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
        std::unique_ptr<VulkanMemoryManager> memory_manager_instance;
        std::unique_ptr<VulkanUploadManager> upload_manager_instance;
        std::unique_ptr<VulkanDescriptorPoolManager> descriptor_pool_manager_instance;
        std::unique_ptr<VulkanDeferredDeletionQueue> deletion_queue;
        std::unique_ptr<VulkanQueue> queue;
        bool initialized = false;
    };

    RHIResult<std::unique_ptr<RHIDevice>> create_vulkan_device();
} // namespace toy3d
