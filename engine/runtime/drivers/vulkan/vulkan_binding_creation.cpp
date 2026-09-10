#include "drivers/vulkan/vulkan_binding_creation.h"

#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_descriptor_pool_manager.h"
#include "drivers/vulkan/vulkan_type_mapping.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace toy3d
{
    namespace
    {
        template <typename T> void append_packet_key(std::string& key, T value)
        {
            static_assert(std::is_trivially_copyable<T>::value, "Packet key fields must be byte-copyable.");
            key.append(reinterpret_cast<const char*>(&value), sizeof(value));
        }

        VkDescriptorType binding_descriptor_type(RHIResourceBindingType type)
        {
            return type == RHIResourceBindingType::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                                                  : to_vk_descriptor_type(type);
        }
    } // namespace

    VulkanPhysicalBindingSources make_vulkan_physical_binding_sources(
        const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings)
    {
        VulkanPhysicalBindingSources physical_sources;
        for (const rhi_detail::ResolvedBinding& resolved : resolved_bindings)
        {
            const std::uint32_t physical_set = VulkanBindingLayout::physical_set(resolved.layout.group);
            if (physical_set < physical_sources.size())
            {
                physical_sources[physical_set].push_back(resolved);
            }
        }
        for (auto& sources : physical_sources)
        {
            std::sort(sources.begin(), sources.end(),
                      [](const rhi_detail::ResolvedBinding& left, const rhi_detail::ResolvedBinding& right)
                      {
                          return left.layout.target_binding != right.layout.target_binding
                                     ? left.layout.target_binding < right.layout.target_binding
                                     : left.value.array_index < right.value.array_index;
                      });
        }
        return physical_sources;
    }

    std::string make_vulkan_binding_packet_cache_key(
        const VulkanBindingLayout& layout, std::uint32_t physical_set,
        const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings)
    {
        std::string key;
        append_packet_key(key, physical_set);
        for (const RHIBindingLayoutEntry& entry : layout.desc().entries)
        {
            if (VulkanBindingLayout::physical_set(entry.group) != physical_set)
                continue;
            append_packet_key(key, entry.binding_id);
            append_packet_key(key, entry.group);
            append_packet_key(key, entry.target_binding);
            append_packet_key(key, entry.type);
            append_packet_key(key, entry.stages);
            append_packet_key(key, entry.array_count);
            append_packet_key(key, entry.data_size);
        }
        for (const rhi_detail::ResolvedBinding& resolved : resolved_bindings)
        {
            append_packet_key(key, resolved.layout.binding_id);
            append_packet_key(key, resolved.value.array_index);
            if (resolved.value.buffer)
            {
                const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(resolved.value.buffer);
                append_packet_key(key, buffer->buffer());
                append_packet_key(key, resolved.layout.data_size);
            }
            else if (resolved.value.texture_view)
            {
                const auto view = std::dynamic_pointer_cast<VulkanTextureView>(resolved.value.texture_view);
                append_packet_key(key, view->image_view());
            }
            else if (resolved.value.sampler)
            {
                const auto sampler = std::dynamic_pointer_cast<VulkanSampler>(resolved.value.sampler);
                append_packet_key(key, sampler->sampler());
            }
        }
        return key;
    }

    RHIResult<std::vector<std::uint32_t>> collect_vulkan_dynamic_uniform_offsets(
        const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings)
    {
        std::vector<std::uint32_t> dynamic_offsets;
        for (const rhi_detail::ResolvedBinding& resolved : resolved_bindings)
        {
            if (resolved.layout.type != RHIResourceBindingType::UniformBuffer)
            {
                continue;
            }
            if (resolved.value.buffer_offset > std::numeric_limits<std::uint32_t>::max())
            {
                return RHIResult<std::vector<std::uint32_t>>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan dynamic uniform-buffer offsets must fit the native 32-bit range.");
            }
            dynamic_offsets.push_back(static_cast<std::uint32_t>(resolved.value.buffer_offset));
        }
        return RHIResult<std::vector<std::uint32_t>>::success(std::move(dynamic_offsets));
    }

    RHIResult<RHIBindingLayoutRef> create_vulkan_binding_layout(const RHIDevice& owner, VkDevice device,
                                                                const RHIBindingLayoutDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIBindingLayoutRef>::failure(
                RHIErrorCode::NotReady, "Vulkan binding-layout creation requires a valid logical device.");
        }
        std::array<std::vector<VkDescriptorSetLayoutBinding>, VulkanBindingLayout::physical_set_count> group_bindings;
        std::set<std::pair<std::uint32_t, std::uint32_t>> occupied_bindings;
        std::uint32_t dynamic_uniform_count = 0;
        for (const RHIBindingLayoutEntry& entry : desc.entries)
        {
            const std::size_t group_index = VulkanBindingLayout::physical_set(entry.group);
            if (group_index >= group_bindings.size())
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::InvalidArgument, "Vulkan binding layout contains an invalid binding group.");
            }
            const VkDescriptorType descriptor_type = binding_descriptor_type(entry.type);
            if (descriptor_type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::Unsupported, "Vulkan binding layout contains an unsupported resource type.");
            }
            if (descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
                ++dynamic_uniform_count > owner.limits().max_dynamic_uniform_buffers)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Vulkan binding layout exceeds the device dynamic uniform-buffer limit.");
            }
            const std::uint32_t native_binding = entry.target_binding;
            if (!occupied_bindings.emplace(static_cast<std::uint32_t>(group_index), native_binding).second)
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
        }

        std::array<VkDescriptorSetLayout, VulkanBindingLayout::physical_set_count> descriptor_set_layouts{};
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
        return RHIResult<RHIBindingLayoutRef>::success(
            std::make_shared<VulkanBindingLayout>(owner, desc, device, descriptor_set_layouts));
    }

    RHIResult<std::shared_ptr<VulkanBindingPacket>> materialize_vulkan_binding_packet(
        const RHIDevice& owner, VkDevice device, VulkanDescriptorPoolManager& descriptor_pool_manager,
        const std::shared_ptr<VulkanBindingLayout>& layout,
        std::uint32_t physical_set, const std::vector<rhi_detail::ResolvedBinding>& resolved_bindings)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::NotReady, "Vulkan binding-packet materialization requires a valid logical device.");
        }
        if (!layout || resolved_bindings.empty() || physical_set >= VulkanBindingLayout::physical_set_count)
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan binding packet requires a layout, valid physical set, and logical sets.");
        }
        if (!layout->is_owned_by(owner))
        {
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                RHIErrorCode::InvalidArgument, "Vulkan binding packet cannot combine objects from different devices.");
        }

        const std::size_t binding_value_count = resolved_bindings.size();
        std::vector<RHIBindingSetRef> source_sets;
        for (const rhi_detail::ResolvedBinding& resolved : resolved_bindings)
        {
            if (!resolved.source_set || !resolved.source_set->is_owned_by(owner) ||
                VulkanBindingLayout::physical_set(resolved.layout.group) != physical_set)
            {
                return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan binding packet contains an incompatible logical binding set.");
            }
            if (std::find(source_sets.begin(), source_sets.end(), resolved.source_set) == source_sets.end())
            {
                source_sets.push_back(resolved.source_set);
            }
        }

        const RHIBindingGroup representative_group = resolved_bindings.front().layout.group;
        const VkDescriptorSetLayout set_layout = layout->descriptor_set_layout(representative_group);
        auto descriptor_allocation = descriptor_pool_manager.allocate(set_layout);
        if (!descriptor_allocation)
            return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                descriptor_allocation.status().code(), descriptor_allocation.status().message());
        const VkDescriptorSet descriptor_set = descriptor_allocation.value().descriptor_set;

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> writes;
        buffer_infos.reserve(binding_value_count);
        image_infos.reserve(binding_value_count);
        writes.reserve(binding_value_count);
        for (const rhi_detail::ResolvedBinding& resolved : resolved_bindings)
        {
            const RHIBindingValue& value = resolved.value;
            const RHIResourceBindingType type = resolved.layout.type;
            VkDescriptorBufferInfo* buffer_info = nullptr;
            VkDescriptorImageInfo* image_info = nullptr;
            if (value.buffer)
            {
                const auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(value.buffer);
                if (!buffer || value.buffer_offset > std::numeric_limits<std::uint32_t>::max())
                {
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        RHIErrorCode::InvalidArgument, "Vulkan uniform binding has an invalid backing buffer or offset.");
                }
                buffer_infos.push_back({buffer->buffer(), 0, resolved.layout.data_size});
                buffer_info = &buffer_infos.back();
            }
            else if (value.texture_view)
            {
                const auto view = std::dynamic_pointer_cast<VulkanTextureView>(value.texture_view);
                if (!view)
                {
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        RHIErrorCode::InvalidArgument, "Vulkan texture binding requires a Vulkan texture view.");
                }
                image_infos.push_back({VK_NULL_HANDLE, view->image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                image_info = &image_infos.back();
            }
            else if (value.sampler)
            {
                const auto sampler = std::dynamic_pointer_cast<VulkanSampler>(value.sampler);
                if (!sampler)
                {
                    return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                        RHIErrorCode::InvalidArgument, "Vulkan sampler binding requires a Vulkan sampler.");
                }
                image_infos.push_back({sampler->sampler(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
                image_info = &image_infos.back();
            }
            else
            {
                return RHIResult<std::shared_ptr<VulkanBindingPacket>>::failure(
                    RHIErrorCode::Unsupported, "Vulkan buffer-view and storage bindings are not implemented yet.");
            }

            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptor_set;
            write.dstBinding = resolved.layout.target_binding;
            write.dstArrayElement = value.array_index;
            write.descriptorCount = 1;
            write.descriptorType = binding_descriptor_type(type);
            write.pBufferInfo = buffer_info;
            write.pImageInfo = image_info;
            writes.push_back(write);
        }
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        return RHIResult<std::shared_ptr<VulkanBindingPacket>>::success(
            std::make_shared<VulkanBindingPacket>(descriptor_allocation.value().page, descriptor_set,
                                                  std::move(source_sets)));
    }
} // namespace toy3d
