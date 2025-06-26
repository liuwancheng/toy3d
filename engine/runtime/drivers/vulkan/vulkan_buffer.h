#pragma once
#include "vk_com.h"
#include "rhi/rhi_resource.h"


namespace toy3d
{
    enum class EIndexBufferType : uint8
    {
        Index16,
        Index32,
    };

    class VulkanBuffer
    {
    public:
        VulkanBuffer() = default;
        VulkanBuffer(VmaAllocator allocator);
        virtual ~VulkanBuffer();

        void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);

        void create_persistent_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);
        
        void create_staging_buffer(VkDeviceSize size);
        
        void copy_buffer(VkCommandBuffer cmd_buffer, VulkanBuffer& src_buffer, VkDeviceSize size, VkDeviceSize src_offset = 0, VkDeviceSize dst_offset = 0);
        
        void* map_memory();
        void unmap_memory();

        void destroy();

        VkBuffer get_native_ptr() const { return vk_buffer; }
        VkDeviceSize get_size() const { return buffer_size; }
        bool is_mapped() const { return mapped_data != nullptr; }

    protected:
        VkBuffer vk_buffer{VK_NULL_HANDLE};
        VmaAllocation vma_allocation{VK_NULL_HANDLE};
        VmaAllocator vma_allocator{VK_NULL_HANDLE};
        VmaAllocationInfo allocation_info{};
        VkDeviceSize buffer_size{0};
        void* mapped_data{nullptr};
    };

    class VulkanIndexBuffer : public RHIIndexBuffer, public VulkanBuffer
    {
    public:
        VulkanIndexBuffer(VmaAllocator allocator, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info);
        virtual ~VulkanIndexBuffer();

        void* map(uint32 offset, uint32 size);
        void unmap();
        
        // 走CommandBuffer Copy流程，需要加Barrier
        void update_data(VkCommandBuffer cmd_buffer, const void* data, uint32 size, uint32 offset = 0);

        EIndexBufferType get_index_type() const { return index_type; }

    private:
        void determine_index_type(uint32 stride);
        bool needs_staging_buffer(EBufferUsageFlags usage) const;
        
        EIndexBufferType index_type;
        bool use_staging_buffer{false};
        std::unique_ptr<VulkanBuffer> staging_buffer;
    };

    class VulkanVertexBuffer : public RHIVertexBuffer, public VulkanBuffer
    {
    public:
        VulkanVertexBuffer(VmaAllocator allocator, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info);
        virtual ~VulkanVertexBuffer();

        void* map(uint32 offset, uint32 size);
        void unmap();
        
        // 走Command Copy流程，需要加Barrier
        void update_data(VkCommandBuffer cmd_buffer,const void* data, uint32 size, uint32 offset = 0);

    private:
        bool needs_staging_buffer(EBufferUsageFlags usage) const;
        
        bool use_staging_buffer{false};
        std::unique_ptr<VulkanBuffer> staging_buffer;
    };

    class VulkanUniformBuffer : public RHIUniformBuffer, public VulkanBuffer
    {
    public:
        VulkanUniformBuffer(VmaAllocator allocator, const RHIUniformBufferLayout& in_layout, EUniformBufferUsage in_usage, const void* data);
        virtual ~VulkanUniformBuffer();

        void update(const void* data);
        
        void* get_mapped_data() { return mapped_data; }
    private:
        uint32 buffer_size;
        bool is_persistent_mapped; // Uniform Buffer保持持久映射
    };

}// namespace toy3d
