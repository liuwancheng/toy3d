#pragma once

#include "rhi/rhi_resource.h"
#include "vk_com.h"

namespace toy3d
{
    class VulkanContext;

    class VulkanBuffer
    {
    public:
        void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& memory);

        void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

        VkBuffer get_native_ptr(){return vk_buffer;};
    protected:
        VkBuffer vk_buffer{VK_NULL_HANDLE};
    };

    class VulkanIndexBuffer : public RHIIndexBuffer, VulkanBuffer
    {
    public:
        VulkanIndexBuffer(VulkanContext* context, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info);

        void* map(VulkanContext* context, uint32 offset, uint32 size);

        void unmap(VulkanContext* context);
    };

    class VulkanVertexBuffer : public RHIVertexBuffer, VulkanBuffer
    {
    public:
        VulkanVertexBuffer(VulkanContext* context, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info);

        void* map(VulkanContext* context, uint32 offset, uint32 size);

        void unmap(VulkanContext* context);
    };

    class VulkanUniformBuffer : public RHIUniformBuffer, VulkanBuffer
    {
    public:
        VulkanUniformBuffer(const RHIUniformBufferLayout& in_layout, EUniformBufferUsage in_usage, const void* data);

        void update(VulkanContext* context, const void* data);
    };
}// namespace toy3d