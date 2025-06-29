#include "vulkan_buffer.h"
#include "vulkan_context.h"
#include <cassert>
#include <cstring>

namespace toy3d
{
    ///////////////////////////////////////////// VulkanBuffer //////////////////////////////////////////////
    VulkanBuffer::VulkanBuffer(VmaAllocator allocator)
    {
        vma_allocator = allocator;
    }

    VulkanBuffer::~VulkanBuffer()
    {
        assert(vk_buffer == VK_NULL_HANDLE && "Buffer not properly destroyed!");
    }

    void VulkanBuffer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage)
    {
        buffer_size = size;

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = memory_usage;

        VkResult result = vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &vk_buffer, &vma_allocation, &allocation_info);
        assert(result == VK_SUCCESS && "Failed to create VMA buffer!");
    }

    void VulkanBuffer::create_persistent_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage)
    {
        buffer_size = size;

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;    // 持久映射
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = memory_usage;

        VkResult result = vmaCreateBuffer(vma_allocator, &buffer_info, &alloc_info, &vk_buffer, &vma_allocation, &allocation_info);
        assert(result == VK_SUCCESS && "Failed to create VMA buffer!");

        mapped_data = allocation_info.pMappedData;
    }

    void VulkanBuffer::create_staging_buffer(VkDeviceSize size)
    {
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    }

    void VulkanBuffer::copy_buffer(VkCommandBuffer cmd_buffer, VulkanBuffer& src_buffer, VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset)
    {
        VkBufferCopy copy_region{};
        copy_region.srcOffset = src_offset;
        copy_region.dstOffset = dst_offset;
        copy_region.size = size;
        
        vkCmdCopyBuffer(cmd_buffer, src_buffer.get_native_ptr(), vk_buffer, 1, &copy_region);
    }

    void* VulkanBuffer::map_memory()
    {
        if (mapped_data == nullptr)
        {
            VkResult result = vmaMapMemory(vma_allocator, vma_allocation, &mapped_data);
            assert(result == VK_SUCCESS && "Failed to map VMA memory!");
        }
        return mapped_data;
    }

    void VulkanBuffer::unmap_memory()
    {
        if (mapped_data != nullptr)
        {
            vmaUnmapMemory(vma_allocator, vma_allocation);
            mapped_data = nullptr;
        }
    }

    void VulkanBuffer::destroy()
    {
        if (mapped_data != nullptr)
        {
            vmaUnmapMemory(vma_allocator, vma_allocation);
            mapped_data = nullptr;
        }

        if (vk_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(vma_allocator, vk_buffer, vma_allocation);
            vk_buffer = VK_NULL_HANDLE;
            vma_allocation = VK_NULL_HANDLE;
        }
    }

    ///////////////////////////////////////////// VulkanIndexBuffer //////////////////////////////////////////////
    
    VulkanIndexBuffer::VulkanIndexBuffer(VmaAllocator allocator, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info)
        : RHIIndexBuffer(in_stride, in_size, in_usage)
        , VulkanBuffer(allocator)
    {
        determine_index_type(in_stride);
        use_staging_buffer = needs_staging_buffer(in_usage);

        VkBufferUsageFlags usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (use_staging_buffer)
        {
            usage_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;
        }
        else
        {
            memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        }

        create_buffer(in_size, usage_flags, memory_usage);
        if (use_staging_buffer)
        {
            staging_buffer = std::make_unique<VulkanBuffer>(allocator);
            staging_buffer->create_staging_buffer(in_size);
        }

        // 初始化数据
        if(create_info.bulk_data)
        {
            void* temp_mapped = map(0, in_size);
            std::memcpy(temp_mapped, create_info.bulk_data, in_size);
            unmap();
        }
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        if(use_staging_buffer && staging_buffer)
        {
            staging_buffer->destroy();
        }
        destroy();
    }

    void VulkanIndexBuffer::determine_index_type(uint32 stride)
    {
        if (stride == sizeof(uint16_t))
        {
            index_type = EIndexBufferType::Index16;
        }
        else if (stride == sizeof(uint32_t))
        {
            index_type = EIndexBufferType::Index32;
        }
        else
        {
            assert(false && "Invalid index buffer stride!");
        }
    }

    bool VulkanIndexBuffer::needs_staging_buffer(EBufferUsageFlags usage) const
    {
        // 如果是静态使用且不需要频繁更新，使用staging buffer获得更好性能
        return (usage & EBufferUsageFlags::BUF_Static) != EBufferUsageFlags::BUF_None ||
               (usage & EBufferUsageFlags::BUF_IndexBuffer) != EBufferUsageFlags::BUF_None;
    }

    void* VulkanIndexBuffer::map(uint32 offset, uint32 size)
    {
        if (use_staging_buffer && staging_buffer)
        {
            return staging_buffer->map_memory();
        }
        else
        {
            return map_memory();
        }
    }

    void VulkanIndexBuffer::unmap()
    {
        if (use_staging_buffer && staging_buffer)
        {
            staging_buffer->unmap_memory();
        }
        else
        {
            unmap_memory();
        }
    }

    void VulkanIndexBuffer::update_data(VkCommandBuffer cmd_buffer, const void* data, uint32 size, uint32 offset)
    {
        if (use_staging_buffer && staging_buffer)
        {
            // 使用staging buffer传输数据
            void* mapped_data = staging_buffer->map_memory();
            std::memcpy(static_cast<char*>(mapped_data) + offset, data, size);
            staging_buffer->unmap_memory();

            // TODO: 需要加Barrier
            copy_buffer(cmd_buffer, *staging_buffer, size, offset, offset);
        }
        else
        {
            // 直接更新
            void* mapped_data = map_memory();
            std::memcpy(static_cast<char*>(mapped_data) + offset, data, size);
            unmap_memory();
        }
    }

    ///////////////////////////////////////////// VulkanVertexBuffer //////////////////////////////////////////////
    
    VulkanVertexBuffer::VulkanVertexBuffer(VmaAllocator allocator, uint32 in_stride, uint32 in_size, EBufferUsageFlags in_usage, RHIResourceCreateInfo& create_info)
        : RHIVertexBuffer(in_size, in_usage)
        , VulkanBuffer(allocator)
    {
        use_staging_buffer = needs_staging_buffer(in_usage);

        VkBufferUsageFlags usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (use_staging_buffer)
        {
            usage_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;
        }
        else
        {
            memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        }

        create_buffer(in_size, usage_flags, memory_usage);
        if (use_staging_buffer)
        {
            staging_buffer = std::make_unique<VulkanBuffer>();
            staging_buffer->create_staging_buffer(in_size);
        }

        // 初始化数据
        if(create_info.bulk_data)
        {
            void* temp_mapped =  map(0, in_size);
            std::memcpy(temp_mapped, create_info.bulk_data, in_size);
            unmap();
        }
    }

    VulkanVertexBuffer::~VulkanVertexBuffer()
    {
        if(use_staging_buffer && staging_buffer)
        {
            staging_buffer->destroy();
        }
        destroy();
    }

    bool VulkanVertexBuffer::needs_staging_buffer(EBufferUsageFlags usage) const
    {
        // 如果是静态使用且不需要频繁更新，使用staging buffer获得更好性能
        return (usage & EBufferUsageFlags::BUF_Static) != EBufferUsageFlags::BUF_None ||
               (usage & EBufferUsageFlags::BUF_VertexBuffer) != EBufferUsageFlags::BUF_None;
    }

    void* VulkanVertexBuffer::map(uint32 offset, uint32 size)
    {
        if (use_staging_buffer && staging_buffer)
        {
            return staging_buffer->map_memory();
        }
        else
        {
            return map_memory();
        }
    }

    void VulkanVertexBuffer::unmap()
    {
        if (use_staging_buffer && staging_buffer)
        {
            staging_buffer->unmap_memory();
        }
        else
        {
            unmap_memory();
        }
    }

    void VulkanVertexBuffer::update_data(VkCommandBuffer cmd_buffer, const void* data, uint32 size, uint32 offset)
    {
        if (use_staging_buffer && staging_buffer)
        {
            // 使用staging buffer传输数据
            void* mapped_data = staging_buffer->map_memory();
            std::memcpy(static_cast<char*>(mapped_data) + offset, data, size);
            staging_buffer->unmap_memory();

            // TODO: 需要加Barrier
            copy_buffer(cmd_buffer, *staging_buffer, size, offset, offset);
        }
        else
        {
            // 直接更新
            void* mapped_data = map_memory();
            std::memcpy(static_cast<char*>(mapped_data) + offset, data, size);
            unmap_memory();
        }
    }

    ///////////////////////////////////////////// VulkanUniformBuffer //////////////////////////////////////////////
    
    VulkanUniformBuffer::VulkanUniformBuffer(VmaAllocator allocator, const RHIUniformBufferLayout& in_layout, EUniformBufferUsage in_usage, const void* data)
        : RHIUniformBuffer(in_layout)
        , VulkanBuffer(allocator)
        , buffer_size(in_layout.buffer_size)
        , is_persistent_mapped(true)
    {
        // 持久映射内存以提高更新性能
        is_persistent_mapped = EUniformBufferUsage::UniformBuffer_MultiFrame == in_usage;
        if (is_persistent_mapped)
        {
            create_persistent_buffer(buffer_size,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        else
        {
            create_buffer(buffer_size,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VMA_MEMORY_USAGE_CPU_TO_GPU);
        }

        // 如果有初始数据，拷贝进去
        update(data);
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        if(is_persistent_mapped)
        {
            if (vk_buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(vma_allocator, vk_buffer, vma_allocation);
                vk_buffer = VK_NULL_HANDLE;
                vma_allocation = VK_NULL_HANDLE;
            }
        }
        else
        {
            destroy();
        }
    }

    void VulkanUniformBuffer::update(const void* data)
    {
        if (data == nullptr) return;

        if (is_persistent_mapped && mapped_data)
        {
            std::memcpy(mapped_data, data, buffer_size);
        }
        else
        {
            void* temp_mapped = map_memory();
            std::memcpy(temp_mapped, data, buffer_size);
            unmap_memory();
        }
    }

}// namespace toy3d
