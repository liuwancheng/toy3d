#include "vulkan_buffer.h"

namespace toy3d
{
     ///////////////////////////////////////////// VulkanBuffer //////////////////////////////////////////////
    void VulkanBuffer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& memory)
    {

    }

    void VulkanBuffer::copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
    {

    }


    ///////////////////////////////////////////// VulkanIndexBuffer //////////////////////////////////////////////
    VulkanIndexBuffer::VulkanIndexBuffer(VulkanContext* context, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info)
    :RHIIndexBuffer(in_stride, in_size, in_usage)
    {

    }

    void* VulkanIndexBuffer::map(VulkanContext* context, uint32 offset, uint32 size)
    {

    }

    void VulkanIndexBuffer::unmap(VulkanContext* context)
    {

    }


    ///////////////////////////////////////////// VulkanVertexBuffer //////////////////////////////////////////////
    VulkanVertexBuffer::VulkanVertexBuffer(VulkanContext* context, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info)
    :RHIVertexBuffer(in_size, in_usage)
    {

    }

    void* VulkanVertexBuffer::map(VulkanContext* context, uint32 offset, uint32 size)
    {

    }

    void VulkanVertexBuffer::unmap(VulkanContext* context)
    {

    }

    ///////////////////////////////////////////// VulkanUniformBuffer //////////////////////////////////////////////
    VulkanUniformBuffer::VulkanUniformBuffer(const RHIUniformBufferLayout& in_layout, EUniformBufferUsage in_usage, const void* data)
    :RHIUniformBuffer(in_layout)
    {

    }

    void VulkanUniformBuffer::update(VulkanContext* context, const void* data)
    {

    }

}// namespace toy3d