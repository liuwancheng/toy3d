#pragma once

#include "drivers/rhi/rhi_device.h"
#include "drivers/vulkan/vulkan_memory_manager.h"
#include "drivers/vulkan/vulkan_upload_manager.h"

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
    class VulkanDeferredDeletionQueue;
    class VulkanQueue;

    struct VulkanDeviceObservation
    {
        VulkanMemoryManagerStats memory;
        VulkanUploadManagerStats upload;
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

        RHIResult<RHIShaderRef> create_shader_impl(const RHIShaderDesc& desc) override;
        RHIResult<RHIBindingLayoutRef> create_binding_layout_impl(
            const RHIBindingLayoutDesc& desc) override;
        RHIResult<RHISamplerRef> create_sampler(
            const RHISamplerDesc& desc) override;
        RHIResult<RHIBindingSetRef> create_binding_set(
            const RHIBindingSetDesc& desc) override;
        RHIResult<std::shared_ptr<VulkanBindingPacket>> materialize_binding_packet(
            const std::shared_ptr<VulkanBindingLayout>& layout,
            std::uint32_t physical_set,
            const std::vector<std::shared_ptr<VulkanBindingSet>>& logical_sets);
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
        VulkanMemoryManager& memory_manager();
        VulkanUploadManager& upload_manager();
        VulkanDeferredDeletionQueue& deferred_deletion_queue();
        VulkanDeviceObservation observation_snapshot() const;
        void release_completed_work(RHIQueueCompletionValue completed_value);

    protected:
        RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const RHIGraphicsPipelineDesc& desc) override;
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
        std::unique_ptr<VulkanDeferredDeletionQueue> deletion_queue;
        std::unique_ptr<VulkanQueue> queue;
        bool initialized = false;
    };

    RHIResult<std::unique_ptr<RHIDevice>> create_vulkan_device();
}
