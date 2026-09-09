#include "drivers/vulkan/vulkan_descriptor_pool_manager.h"

#include "drivers/vulkan/vulkan_type_mapping.h"

#include <array>

namespace toy3d
{
    VulkanDescriptorPoolPage::VulkanDescriptorPoolPage(VkDevice device, VkDescriptorPool pool,
                                                       std::uint32_t capacity)
        : vk_device(device), vk_pool(pool), set_capacity(capacity)
    {
    }

    VulkanDescriptorPoolPage::~VulkanDescriptorPoolPage()
    {
        if (vk_device != VK_NULL_HANDLE && vk_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(vk_device, vk_pool, nullptr);
    }

    bool VulkanDescriptorPoolPage::has_capacity() const
    {
        return allocated_set_count < set_capacity;
    }

    RHIResult<VkDescriptorSet> VulkanDescriptorPoolPage::allocate(VkDescriptorSetLayout layout)
    {
        if (!has_capacity() || layout == VK_NULL_HANDLE)
            return RHIResult<VkDescriptorSet>::failure(RHIErrorCode::OutOfMemory,
                                                       "Vulkan descriptor pool page is full.");
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = vk_pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const RHIStatus status = vulkan_status_from_result(vkAllocateDescriptorSets(vk_device, &info, &set),
                                                           "vkAllocateDescriptorSets");
        if (!status)
            return RHIResult<VkDescriptorSet>::failure(status.code(), status.message());
        ++allocated_set_count;
        return RHIResult<VkDescriptorSet>::success(set);
    }

    RHIStatus VulkanDescriptorPoolPage::reset()
    {
        const RHIStatus status = vulkan_status_from_result(vkResetDescriptorPool(vk_device, vk_pool, 0),
                                                           "vkResetDescriptorPool");
        if (status)
            allocated_set_count = 0;
        return status;
    }

    VulkanDescriptorPoolManager::VulkanDescriptorPoolManager(VkDevice device) : vk_device(device) {}

    RHIResult<VulkanDescriptorAllocation> VulkanDescriptorPoolManager::allocate(VkDescriptorSetLayout layout)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (const std::shared_ptr<VulkanDescriptorPoolPage>& page : pages)
        {
            if (!page->has_capacity() && page.use_count() == 1)
            {
                const RHIStatus reset_status = page->reset();
                if (!reset_status)
                    return RHIResult<VulkanDescriptorAllocation>::failure(reset_status.code(), reset_status.message());
                ++manager_stats.page_resets;
            }
            if (!page->has_capacity())
                continue;
            auto set = page->allocate(layout);
            if (set)
            {
                ++manager_stats.set_allocations;
                return RHIResult<VulkanDescriptorAllocation>::success({page, set.value()});
            }
        }
        auto page = create_page();
        if (!page)
            return RHIResult<VulkanDescriptorAllocation>::failure(page.status().code(), page.status().message());
        pages.push_back(page.value());
        auto set = page.value()->allocate(layout);
        if (!set)
            return RHIResult<VulkanDescriptorAllocation>::failure(set.status().code(), set.status().message());
        ++manager_stats.set_allocations;
        return RHIResult<VulkanDescriptorAllocation>::success({page.value(), set.value()});
    }

    RHIResult<std::shared_ptr<VulkanDescriptorPoolPage>> VulkanDescriptorPoolManager::create_page()
    {
        const std::array<VkDescriptorPoolSize, 7> sizes = {{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, sets_per_page * 8u},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sets_per_page * 16u},
            {VK_DESCRIPTOR_TYPE_SAMPLER, sets_per_page * 16u},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, sets_per_page * 8u},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sets_per_page * 8u},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, sets_per_page * 8u},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets_per_page * 8u},
        }};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = sets_per_page;
        info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        VkDescriptorPool pool = VK_NULL_HANDLE;
        const RHIStatus status =
            vulkan_status_from_result(vkCreateDescriptorPool(vk_device, &info, nullptr, &pool),
                                      "vkCreateDescriptorPool");
        if (!status)
            return RHIResult<std::shared_ptr<VulkanDescriptorPoolPage>>::failure(status.code(), status.message());
        ++manager_stats.page_creations;
        return RHIResult<std::shared_ptr<VulkanDescriptorPoolPage>>::success(
            std::make_shared<VulkanDescriptorPoolPage>(vk_device, pool, sets_per_page));
    }

    void VulkanDescriptorPoolManager::record_packet_materialization()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        ++manager_stats.packet_materializations;
    }

    void VulkanDescriptorPoolManager::record_packet_cache_hit()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        ++manager_stats.packet_cache_hits;
    }

    VulkanDescriptorPoolManagerStats VulkanDescriptorPoolManager::statistics() const
    {
        const std::lock_guard<std::mutex> lock(mutex);
        VulkanDescriptorPoolManagerStats result = manager_stats;
        result.page_count = pages.size();
        return result;
    }

    void VulkanDescriptorPoolManager::shutdown()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        pages.clear();
        vk_device = VK_NULL_HANDLE;
    }
} // namespace toy3d
