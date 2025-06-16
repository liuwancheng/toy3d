#include "vulkan_buffer.h"

namespace toy3d
{
    ///////////////////////////////////////////// VulkanIndexBuffer //////////////////////////////////////////////
    VulkanIndexBuffer::VulkanIndexBuffer(VkDevice* device, uint32 in_stride, uint32 in_size, uint32 in_usage, RHIResourceCreateInfo& create_info)
    {

    }

    void* VulkanIndexBuffer::map(VkDevice* device, uint32 offset, uint32 size)
    {

    }

    void VulkanIndexBuffer::unmap(VkDevice* device)
    {

    }


    ///////////////////////////////////////////// VulkanVertexBuffer //////////////////////////////////////////////
    VulkanVertexBuffer::VulkanVertexBuffer(VkDevice* device, uint32 in_stride, uint32 in_size, uint32 in_usage, RHIResourceCreateInfo& create_info)
    {

    }

    void* VulkanVertexBuffer::map(VkDevice* device, uint32 offset, uint32 size)
    {

    }

    void VulkanVertexBuffer::unmap(VkDevice* device)
    {

    }

    ///////////////////////////////////////////// VulkanUniformBuffer //////////////////////////////////////////////
    VulkanUniformBuffer::VulkanUniformBuffer(const RHIUniformBufferLayout& in_layout, EUniformBufferUsage in_usage, const void* data)
    {

    }

    void VulkanUniformBuffer::update(VkDevice* device, const void* data)
    {

    }

}// namespace toy3d