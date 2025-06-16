#pragma once

#include "rhi/rhi_resource.h"
#include "vk_com.h"

namespace toy3d
{
    class VulkanContext;

    class VulkanIndexBuffer : public RHIIndexBuffer
    {
    public:
        VulkanIndexBuffer(VkDevice* device, uint32 in_stride, uint32 in_size, uint32 in_usage, RHIResourceCreateInfo& create_info);

        void* map(VkDevice* device, uint32 offset, uint32 size);

        void unmap(VkDevice* device);
    private:
        VkBuffer vk_buffer;
    };

    class VulkanVertexBuffer : public RHIVertexBuffer
    {
    public:
        VulkanVertexBuffer(VkDevice* device, uint32 in_stride, uint32 in_size, uint32 in_usage, RHIResourceCreateInfo& create_info);

        void* map(VkDevice* device, uint32 offset, uint32 size);

        void unmap(VkDevice* device);
    private:
        VkBuffer vk_buffer;
    };

    class VulkanUniformBuffer : public RHIUniformBuffer
    {
    public:
        VulkanUniformBuffer(const RHIUniformBufferLayout& in_layout, EUniformBufferUsage in_usage, const void* data);

        void update(VkDevice* device, const void* data);
    private:
        VkBuffer vk_buffer;
    };
}// namespace toy3d