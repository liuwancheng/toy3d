#include "vulkan_descriptor_sets.h"
#include "vulkan_context.h"
#include <cassert>
#include <functional>

namespace toy3d
{
    ////////////////////////////////// VulkanDescriptorSetLayout //////////////////////////////////

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice device, const std::vector<DescriptorBinding>& bindings)
        : device(device), bindings(bindings), descriptor_set_layout(VK_NULL_HANDLE)
    {
        layout_hash = calculate_hash();

        std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
        vk_bindings.reserve(bindings.size());

        for (const auto& binding : bindings)
        {
            VkDescriptorSetLayoutBinding vk_binding{};
            vk_binding.binding = binding.binding;
            vk_binding.descriptorType = convert_descriptor_type(binding.type);
            vk_binding.descriptorCount = binding.count;
            vk_binding.stageFlags = binding.stages;
            vk_binding.pImmutableSamplers = nullptr;

            vk_bindings.push_back(vk_binding);
        }

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = static_cast<uint32>(vk_bindings.size());
        layout_info.pBindings = vk_bindings.data();

        VkResult result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout);
        assert(result == VK_SUCCESS && "Failed to create descriptor set layout!");
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
    {
        if (descriptor_set_layout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
        }
    }

    uint32 VulkanDescriptorSetLayout::calculate_hash() const
    {
        size_t hash = 0;
        for (const auto& binding : bindings)
        {
            hash ^= std::hash<uint32>{}(binding.binding) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint8>{}(static_cast<uint8>(binding.type)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(binding.count) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(binding.stages) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return static_cast<uint32>(hash);
    }

    VkDescriptorType VulkanDescriptorSetLayout::convert_descriptor_type(EDescriptorType type) const
    {
        switch (type)
        {
            case EDescriptorType::UniformBuffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case EDescriptorType::StorageBuffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case EDescriptorType::CombinedImageSampler:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case EDescriptorType::SampledImage:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case EDescriptorType::StorageImage:
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case EDescriptorType::Sampler:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case EDescriptorType::InputAttachment:
                return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            default:
                assert(false && "Unknown descriptor type!");
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
    }

    ////////////////////////////////// DescriptorResource //////////////////////////////////

    DescriptorResource DescriptorResource::create_buffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, EDescriptorType type)
    {
        DescriptorResource resource;
        resource.type = type;
        resource.buffer_info.buffer = buffer;
        resource.buffer_info.offset = offset;
        resource.buffer_info.range = range;
        return resource;
    }

    DescriptorResource DescriptorResource::create_image(VkImageView image_view, VkSampler sampler, VkImageLayout layout)
    {
        DescriptorResource resource;
        resource.type = EDescriptorType::CombinedImageSampler;
        resource.image_info.image_view = image_view;
        resource.image_info.sampler = sampler;
        resource.image_info.layout = layout;
        return resource;
    }

    DescriptorResource DescriptorResource::create_sampler(VkSampler sampler)
    {
        DescriptorResource resource;
        resource.type = EDescriptorType::Sampler;
        resource.image_info.sampler = sampler;
        resource.image_info.image_view = VK_NULL_HANDLE;
        resource.image_info.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return resource;
    }

    ////////////////////////////////// VulkanDescriptorSet //////////////////////////////////

    VulkanDescriptorSet::VulkanDescriptorSet(VkDevice device, VkDescriptorSet descriptor_set, 
                                           std::shared_ptr<VulkanDescriptorSetLayout> layout)
        : device(device), descriptor_set(descriptor_set), layout(layout)
    {
    }

    void VulkanDescriptorSet::update_descriptor(uint32 binding, const DescriptorResource& resource, uint32 array_index)
    {
        write_descriptor(binding, resource, array_index);
        mark_dirty();
    }

    void VulkanDescriptorSet::update_descriptor(uint32 binding, const std::vector<DescriptorResource>& resources)
    {
        for (uint32 i = 0; i < resources.size(); ++i)
        {
            write_descriptor(binding, resources[i], i);
        }
        mark_dirty();
    }

    void VulkanDescriptorSet::update_descriptors(const std::unordered_map<uint32, DescriptorResource>& resources)
    {
        for (const auto& [binding, resource] : resources)
        {
            write_descriptor(binding, resource, 0);
        }
        mark_dirty();
    }

    void VulkanDescriptorSet::write_descriptor(uint32 binding, const DescriptorResource& resource, uint32 array_index)
    {
        VkWriteDescriptorSet descriptor_write{};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = descriptor_set;
        descriptor_write.dstBinding = binding;
        descriptor_write.dstArrayElement = array_index;
        descriptor_write.descriptorCount = 1;

        VkDescriptorBufferInfo buffer_info{};
        VkDescriptorImageInfo image_info{};

        switch (resource.type)
        {
            case EDescriptorType::UniformBuffer:
            case EDescriptorType::StorageBuffer:
                descriptor_write.descriptorType = (resource.type == EDescriptorType::UniformBuffer) 
                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                buffer_info.buffer = resource.buffer_info.buffer;
                buffer_info.offset = resource.buffer_info.offset;
                buffer_info.range = resource.buffer_info.range;
                descriptor_write.pBufferInfo = &buffer_info;
                break;

            case EDescriptorType::CombinedImageSampler:
                descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                image_info.imageView = resource.image_info.image_view;
                image_info.sampler = resource.image_info.sampler;
                image_info.imageLayout = resource.image_info.layout;
                descriptor_write.pImageInfo = &image_info;
                break;

            case EDescriptorType::SampledImage:
                descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                image_info.imageView = resource.image_info.image_view;
                image_info.imageLayout = resource.image_info.layout;
                descriptor_write.pImageInfo = &image_info;
                break;

            case EDescriptorType::Sampler:
                descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                image_info.sampler = resource.image_info.sampler;
                descriptor_write.pImageInfo = &image_info;
                break;

            default:
                assert(false && "Unsupported descriptor type for writing!");
                return;
        }

        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);
    }

    ////////////////////////////////// VulkanDescriptorPool //////////////////////////////////

    VulkanDescriptorPool::VulkanDescriptorPool(VkDevice device, uint32 max_sets)
        : device(device), max_sets(max_sets), descriptor_pool(VK_NULL_HANDLE)
    {
        create_pool();
    }

    VulkanDescriptorPool::~VulkanDescriptorPool()
    {
        if (descriptor_pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        }
    }

    void VulkanDescriptorPool::create_pool()
    {
        // 定义描述符池大小，支持常见的描述符类型
        std::vector<VkDescriptorPoolSize> pool_sizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_sets * 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, max_sets * 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sets * 8},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_sets * 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_sets * 2},
            {VK_DESCRIPTOR_TYPE_SAMPLER, max_sets * 4}
        };

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = max_sets;
        pool_info.poolSizeCount = static_cast<uint32>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        VkResult result = vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool);
        assert(result == VK_SUCCESS && "Failed to create descriptor pool!");
    }

    VkDescriptorSet VulkanDescriptorPool::allocate_descriptor_set(VkDescriptorSetLayout layout)
    {
        if (allocated_sets >= max_sets)
        {
            return VK_NULL_HANDLE; // 池已满
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout;

        VkDescriptorSet descriptor_set;
        VkResult result = vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set);
        
        if (result == VK_SUCCESS)
        {
            allocated_sets++;
            return descriptor_set;
        }
        
        return VK_NULL_HANDLE;
    }

    void VulkanDescriptorPool::reset_pool()
    {
        vkResetDescriptorPool(device, descriptor_pool, 0);
        allocated_sets = 0;
    }

    ////////////////////////////////// VulkanDescriptorSetManager //////////////////////////////////

    VulkanDescriptorSetManager::VulkanDescriptorSetManager(VulkanContext* context)
        : context(context), device(context->device)
    {
        // 创建初始描述符池
        descriptor_pools.push_back(create_new_pool());
    }

    VulkanDescriptorSetManager::~VulkanDescriptorSetManager()
    {
        layout_cache.clear();
        descriptor_pools.clear();
    }

    std::shared_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetManager::create_descriptor_set_layout(
        const std::vector<DescriptorBinding>& bindings)
    {
        return std::make_shared<VulkanDescriptorSetLayout>(device, bindings);
    }

    std::shared_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetManager::get_or_create_layout(
        const std::vector<DescriptorBinding>& bindings)
    {
        // 临时创建布局来计算hash
        auto temp_layout = std::make_shared<VulkanDescriptorSetLayout>(device, bindings);
        uint32 hash = temp_layout->get_hash();

        auto it = layout_cache.find(hash);
        if (it != layout_cache.end())
        {
            return it->second;
        }

        layout_cache[hash] = temp_layout;
        return temp_layout;
    }

    std::unique_ptr<VulkanDescriptorSet> VulkanDescriptorSetManager::allocate_descriptor_set(
        std::shared_ptr<VulkanDescriptorSetLayout> layout)
    {
        VulkanDescriptorPool* pool = get_available_pool();
        if (!pool)
        {
            // 创建新池
            descriptor_pools.push_back(create_new_pool());
            pool = descriptor_pools.back().get();
        }

        VkDescriptorSet vk_set = pool->allocate_descriptor_set(layout->get_layout());
        if (vk_set == VK_NULL_HANDLE)
        {
            // 当前池已满，创建新池
            descriptor_pools.push_back(create_new_pool());
            pool = descriptor_pools.back().get();
            vk_set = pool->allocate_descriptor_set(layout->get_layout());
        }

        assert(vk_set != VK_NULL_HANDLE && "Failed to allocate descriptor set!");
        return std::make_unique<VulkanDescriptorSet>(device, vk_set, layout);
    }

    std::shared_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetManager::create_ubo_layout(
        uint32 binding, VkShaderStageFlags stages)
    {
        std::vector<DescriptorBinding> bindings = {
            DescriptorBinding(binding, EDescriptorType::UniformBuffer, 1, stages)
        };
        return get_or_create_layout(bindings);
    }

    std::shared_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetManager::create_texture_layout(
        uint32 binding, VkShaderStageFlags stages)
    {
        std::vector<DescriptorBinding> bindings = {
            DescriptorBinding(binding, EDescriptorType::CombinedImageSampler, 1, stages)
        };
        return get_or_create_layout(bindings);
    }

    std::shared_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetManager::create_combined_layout(
        const std::vector<std::pair<uint32, EDescriptorType>>& descriptor_types,
        VkShaderStageFlags stages)
    {
        std::vector<DescriptorBinding> bindings;
        for (const auto& [binding, type] : descriptor_types)
        {
            bindings.emplace_back(binding, type, 1, stages);
        }
        return get_or_create_layout(bindings);
    }

    void VulkanDescriptorSetManager::reset_all_pools()
    {
        for (auto& pool : descriptor_pools)
        {
            pool->reset_pool();
        }
        current_pool_index = 0;
    }

    void VulkanDescriptorSetManager::begin_frame()
    {
        // 每帧开始时可以进行一些清理工作
        // 例如重置池，清理过时的缓存等
    }

    std::unique_ptr<VulkanDescriptorPool> VulkanDescriptorSetManager::create_new_pool()
    {
        return std::make_unique<VulkanDescriptorPool>(device, 1000); // 每个池支持1000个描述符集
    }

    VulkanDescriptorPool* VulkanDescriptorSetManager::get_available_pool()
    {
        if (current_pool_index < descriptor_pools.size())
        {
            return descriptor_pools[current_pool_index].get();
        }
        return nullptr;
    }

    ////////////////////////////////// VulkanDescriptorSetBinder //////////////////////////////////

    VulkanDescriptorSetBinder::VulkanDescriptorSetBinder()
    {
    }

    void VulkanDescriptorSetBinder::bind_descriptor_set(uint32 set_index, VulkanDescriptorSet* descriptor_set)
    {
        auto& binding = bound_sets[set_index];
        if (binding.descriptor_set != descriptor_set || descriptor_set->is_dirty_data())
        {
            binding.descriptor_set = descriptor_set;
            binding.last_update_frame = current_frame;
            needs_rebind_flag = true;
            
            if (descriptor_set)
            {
                descriptor_set->clear_dirty();
            }
        }
    }

    void VulkanDescriptorSetBinder::bind_to_command_buffer(VkCommandBuffer cmd_buffer, VkPipelineLayout pipeline_layout, 
                                                          VkPipelineBindPoint bind_point)
    {
        if (!needs_rebind_flag)
        {
            return;
        }

        std::vector<VkDescriptorSet> descriptor_sets;
        std::vector<uint32> dynamic_offsets;

        // 按set索引排序绑定
        for (const auto& [set_index, binding] : bound_sets)
        {
            if (binding.descriptor_set)
            {
                // 确保索引连续
                if (descriptor_sets.size() <= set_index)
                {
                    descriptor_sets.resize(set_index + 1, VK_NULL_HANDLE);
                }
                descriptor_sets[set_index] = binding.descriptor_set->get_descriptor_set();
            }
        }

        if (!descriptor_sets.empty())
        {
            vkCmdBindDescriptorSets(cmd_buffer, bind_point, pipeline_layout,
                                   0, static_cast<uint32>(descriptor_sets.size()), descriptor_sets.data(),
                                   0, nullptr);
        }

        needs_rebind_flag = false;
    }

    void VulkanDescriptorSetBinder::clear_bindings()
    {
        bound_sets.clear();
        needs_rebind_flag = true;
    }

    bool VulkanDescriptorSetBinder::needs_rebind() const
    {
        return needs_rebind_flag;
    }

} // namespace toy3d