#pragma once

#include "platform/platform_defines.h"

#include "drivers/rhi/rhi_result.h"

#if WITH_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace toy3d
{
    class VulkanDescriptorPoolPage final
    {
      public:
        VulkanDescriptorPoolPage(VkDevice device, VkDescriptorPool pool, std::uint32_t capacity);
        ~VulkanDescriptorPoolPage();

        VulkanDescriptorPoolPage(const VulkanDescriptorPoolPage&) = delete;
        VulkanDescriptorPoolPage& operator=(const VulkanDescriptorPoolPage&) = delete;

        RHIResult<VkDescriptorSet> allocate(VkDescriptorSetLayout layout);
        bool has_capacity() const;
        RHIStatus reset();

      private:
        VkDevice vk_device = VK_NULL_HANDLE;
        VkDescriptorPool vk_pool = VK_NULL_HANDLE;
        std::uint32_t set_capacity = 0;
        std::uint32_t allocated_set_count = 0;
    };

    struct VulkanDescriptorAllocation
    {
        std::shared_ptr<VulkanDescriptorPoolPage> page;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };

    struct VulkanDescriptorPoolManagerStats
    {
        std::uint64_t page_creations = 0;
        std::uint64_t page_resets = 0;
        std::uint64_t set_allocations = 0;
        std::uint64_t packet_materializations = 0;
        std::uint64_t packet_cache_hits = 0;
        std::size_t page_count = 0;
    };

    class VulkanDescriptorPoolManager final
    {
      public:
        explicit VulkanDescriptorPoolManager(VkDevice device);
        ~VulkanDescriptorPoolManager() = default;

        RHIResult<VulkanDescriptorAllocation> allocate(VkDescriptorSetLayout layout);
        void record_packet_materialization();
        void record_packet_cache_hit();
        VulkanDescriptorPoolManagerStats statistics() const;
        void shutdown();

      private:
        RHIResult<std::shared_ptr<VulkanDescriptorPoolPage>> create_page();

        static constexpr std::uint32_t sets_per_page = 128u;
        mutable std::mutex mutex;
        VkDevice vk_device = VK_NULL_HANDLE;
        std::vector<std::shared_ptr<VulkanDescriptorPoolPage>> pages;
        VulkanDescriptorPoolManagerStats manager_stats;
    };
} // namespace toy3d
