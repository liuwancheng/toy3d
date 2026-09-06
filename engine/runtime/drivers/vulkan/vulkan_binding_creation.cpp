#include "drivers/vulkan/vulkan_binding_creation.h"

#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_type_mapping.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

namespace toy3d
{
    RHIResult<RHIBindingLayoutRef> create_vulkan_binding_layout(
        const RHIDevice& owner,
        VkDevice device,
        const RHIBindingLayoutDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIBindingLayoutRef>::failure(
                RHIErrorCode::NotReady,
                "Vulkan binding-layout creation requires a valid logical device.");
        }
        std::array<
            std::vector<VkDescriptorSetLayoutBinding>,
            VulkanBindingLayout::physical_set_count> group_bindings;
        std::vector<VulkanBindingLayout::NativeBinding> native_bindings;
        std::set<std::pair<std::uint32_t, std::uint32_t>> occupied_bindings;
        native_bindings.reserve(desc.entries.size());
        for (const RHIBindingLayoutEntry& entry : desc.entries)
        {
            const std::size_t group_index = VulkanBindingLayout::physical_set(entry.group);
            if (group_index >= group_bindings.size())
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding layout contains an invalid binding group.");
            }
            const VkDescriptorType descriptor_type = to_vk_descriptor_type(entry.type);
            if (descriptor_type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan binding layout contains an unsupported resource type.");
            }
            const std::uint32_t native_binding = entry.slot;
            if (!occupied_bindings.emplace(
                    static_cast<std::uint32_t>(group_index), native_binding).second)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding layout contains duplicate bindings in one physical set.");
            }
            VkDescriptorSetLayoutBinding layout_binding{};
            layout_binding.binding = native_binding;
            layout_binding.descriptorType = descriptor_type;
            layout_binding.descriptorCount = entry.array_count;
            layout_binding.stageFlags = to_vk_shader_stage_flags(entry.stages);
            group_bindings[group_index].push_back(layout_binding);
            native_bindings.push_back({entry.group, entry.slot, entry.type, native_binding});
        }

        std::array<
            VkDescriptorSetLayout,
            VulkanBindingLayout::physical_set_count> descriptor_set_layouts{};
        for (std::size_t group_index = 0; group_index < group_bindings.size(); ++group_index)
        {
            VkDescriptorSetLayoutCreateInfo create_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            create_info.bindingCount = static_cast<std::uint32_t>(group_bindings[group_index].size());
            create_info.pBindings = group_bindings[group_index].data();
            const RHIStatus create_status = vulkan_status_from_result(
                vkCreateDescriptorSetLayout(device, &create_info, nullptr, &descriptor_set_layouts[group_index]),
                "vkCreateDescriptorSetLayout");
            if (!create_status)
            {
                for (VkDescriptorSetLayout layout : descriptor_set_layouts)
                {
                    if (layout != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorSetLayout(device, layout, nullptr);
                    }
                }
                return RHIResult<RHIBindingLayoutRef>::failure(create_status.code(), create_status.message());
            }
        }
        return RHIResult<RHIBindingLayoutRef>::success(std::make_shared<VulkanBindingLayout>(
            owner, desc, device, descriptor_set_layouts, std::move(native_bindings)));
    }

    RHIResult<RHIBindingSetRef> create_vulkan_binding_set(const RHIBindingSetDesc& desc)
    {
        const auto layout = std::dynamic_pointer_cast<VulkanBindingLayout>(desc.layout);
        if (!layout)
        {
            return RHIResult<RHIBindingSetRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan binding set requires a layout created by the Vulkan device.");
        }
        for (const RHIBindingLayoutEntry& entry : desc.layout->desc().entries)
        {
            if (entry.group != desc.group)
            {
                continue;
            }
            if (entry.type == RHIResourceBindingType::ReadOnlyBuffer ||
                entry.type == RHIResourceBindingType::StorageBuffer ||
                entry.type == RHIResourceBindingType::StorageTexture)
            {
                return RHIResult<RHIBindingSetRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan binding sets currently support uniform buffers, sampled textures, and samplers only.");
            }
        }
        for (const RHIBindingValue& value : desc.bindings)
        {
            if (value.buffer)
            {
                const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(value.buffer);
                if (!buffer)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan uniform-buffer binding requires a Vulkan buffer and an aligned offset.");
                }
            }
            else if (value.texture_view)
            {
                const auto view = std::dynamic_pointer_cast<VulkanTextureView>(value.texture_view);
                if (!view)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan sampled-texture binding requires a Vulkan texture view.");
                }
            }
            else if (value.sampler)
            {
                const auto sampler = std::dynamic_pointer_cast<VulkanSampler>(value.sampler);
                if (!sampler)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::InvalidArgument,
                        "Vulkan sampler binding requires a sampler created by the Vulkan device.");
                }
            }
            else
            {
                return RHIResult<RHIBindingSetRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan buffer-view and storage bindings are not implemented yet.");
            }
        }
        return RHIResult<RHIBindingSetRef>::success(
            std::make_shared<VulkanBindingSet>(desc));
    }

    RHIResult<std::shared_ptr<VulkanBindingPacket>> materialize_vulkan_binding_packet(
        const RHIDevice& owner,
        VkDevice device,
        const std::shared_ptr<VulkanBindingLayout>& layout,
        std::uint32_t physical_set,
        const std::vector<std::shared_ptr<VulkanBindingSet>>& logical_sets)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::NotReady,
                "Vulkan binding-packet materialization requires a valid logical device.");
        }
        if (!layout || logical_sets.empty() ||
            physical_set >= VulkanBindingLayout::physical_set_count)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan binding packet requires a layout, valid physical set, and logical sets.");
        }
        if (!layout->is_owned_by(owner) ||
            std::any_of(logical_sets.begin(), logical_sets.end(), [&owner](
                const std::shared_ptr<VulkanBindingSet>& logical_set)
            {
                return !logical_set || !logical_set->is_owned_by(owner);
            }))
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan binding packet cannot combine objects from different devices.");
        }

        std::size_t binding_value_count = 0;
        std::map<VkDescriptorType, std::uint32_t> descriptor_counts;
        for (const std::shared_ptr<VulkanBindingSet>& logical_set : logical_sets)
        {
            if (!logical_set ||
                !(logical_set->layout()->desc() == layout->desc()) ||
                VulkanBindingLayout::physical_set(logical_set->group()) != physical_set)
            {
                return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding packet contains an incompatible logical binding set.");
            }
            binding_value_count += logical_set->desc().bindings.size();
            for (const RHIBindingLayoutEntry& entry : layout->desc().entries)
            {
                if (entry.group == logical_set->group())
                {
                    descriptor_counts[to_vk_descriptor_type(entry.type)] += entry.array_count;
                }
            }
        }

        std::vector<VkDescriptorPoolSize> pool_sizes;
        pool_sizes.reserve(descriptor_counts.size());
        for (const auto& descriptor_count : descriptor_counts)
        {
            pool_sizes.push_back({descriptor_count.first, descriptor_count.second});
        }
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        RHIStatus status = vulkan_status_from_result(
            vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool),
            "vkCreateDescriptorPool");
        if (!status)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                status.code(), status.message());
        }

        const RHIBindingGroup representative_group = logical_sets.front()->group();
        const VkDescriptorSetLayout set_layout = layout->descriptor_set_layout(representative_group);
        VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate_info.descriptorPool = descriptor_pool;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &set_layout;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        status = vulkan_status_from_result(
            vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set),
            "vkAllocateDescriptorSets");
        if (!status)
        {
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                status.code(), status.message());
        }

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> writes;
        buffer_infos.reserve(binding_value_count);
        image_infos.reserve(binding_value_count);
        writes.reserve(binding_value_count);
        for (const std::shared_ptr<VulkanBindingSet>& logical_set : logical_sets)
        {
            for (const RHIBindingValue& value : logical_set->desc().bindings)
            {
                RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
                VkDescriptorBufferInfo* buffer_info = nullptr;
                VkDescriptorImageInfo* image_info = nullptr;
                if (value.buffer)
                {
                    const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(value.buffer);
                    const std::uint64_t range = value.buffer_size == 0
                        ? value.buffer->desc().size - value.buffer_offset
                        : value.buffer_size;
                    buffer_infos.push_back({buffer->buffer(), value.buffer_offset, range});
                    buffer_info = &buffer_infos.back();
                }
                else if (value.texture_view)
                {
                    type = RHIResourceBindingType::SampledTexture;
                    const auto view = std::dynamic_pointer_cast<VulkanTextureView>(value.texture_view);
                    image_infos.push_back({
                        VK_NULL_HANDLE, view->image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                    image_info = &image_infos.back();
                }
                else if (value.sampler)
                {
                    type = RHIResourceBindingType::Sampler;
                    const auto sampler = std::dynamic_pointer_cast<VulkanSampler>(value.sampler);
                    image_infos.push_back({
                        sampler->sampler(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
                    image_info = &image_infos.back();
                }
                else
                {
                    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        RHIErrorCode::Unsupported,
                        "Vulkan buffer-view and storage bindings are not implemented yet.");
                }

                const auto native_binding = layout->native_binding(
                    logical_set->group(), type, value.slot);
                if (!native_binding)
                {
                    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        native_binding.status().code(), native_binding.status().message());
                }
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = descriptor_set;
                write.dstBinding = native_binding.value();
                write.dstArrayElement = value.array_index;
                write.descriptorCount = 1;
                write.descriptorType = to_vk_descriptor_type(type);
                write.pBufferInfo = buffer_info;
                write.pImageInfo = image_info;
                writes.push_back(write);
            }
        }
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        return RHIResult<std::shared_ptr<VulkanBindingPacket>>::success(
            std::make_shared<VulkanBindingPacket>(
                device, descriptor_pool, descriptor_set, logical_sets));
    }
}
