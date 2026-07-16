#pragma once

#include "vk_com.h"
#include "rhi/rhi_legacy_resource.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace toy3d
{
    class VulkanContext;
    class VulkanBuffer;
    class VulkanTexture;
    class VulkanSamplerState;

    // 描述符绑定类型
    enum class EDescriptorType : uint8
    {
        UniformBuffer,
        StorageBuffer,
        CombinedImageSampler,
        SampledImage,
        StorageImage,
        Sampler,
        InputAttachment
    };

    // 描述符绑定信息
    struct DescriptorBinding
    {
        uint32 binding;
        EDescriptorType type;
        uint32 count;
        VkShaderStageFlags stages;
        
        DescriptorBinding() = default;
        DescriptorBinding(uint32 in_binding, EDescriptorType in_type, uint32 in_count, VkShaderStageFlags in_stages)
            : binding(in_binding), type(in_type), count(in_count), stages(in_stages) {}
    };

    // 描述符集布局
    class VulkanDescriptorSetLayout
    {
    public:
        VulkanDescriptorSetLayout(VkDevice device, const std::vector<DescriptorBinding>& bindings);
        ~VulkanDescriptorSetLayout();

        VkDescriptorSetLayout get_layout() const { return descriptor_set_layout; }
        const std::vector<DescriptorBinding>& get_bindings() const { return bindings; }
        uint32 get_hash() const { return layout_hash; }

        // 禁用拷贝
        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
        VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;

    private:
        VkDevice device;
        VkDescriptorSetLayout descriptor_set_layout;
        std::vector<DescriptorBinding> bindings;
        uint32 layout_hash;

        uint32 calculate_hash() const;
        VkDescriptorType convert_descriptor_type(EDescriptorType type) const;
    };

    // 描述符集资源绑定
    struct DescriptorResource
    {
        EDescriptorType type;
        union
        {
            struct
            {
                VkBuffer buffer;
                VkDeviceSize offset;
                VkDeviceSize range;
            } buffer_info;
            
            struct
            {
                VkImageView image_view;
                VkSampler sampler;
                VkImageLayout layout;
            } image_info;
        };

        DescriptorResource() = default;
        
        // 创建缓冲区描述符
        static DescriptorResource create_buffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, EDescriptorType type);
        
        // 创建图像描述符
        static DescriptorResource create_image(VkImageView image_view, VkSampler sampler, VkImageLayout layout);
        
        // 创建采样器描述符
        static DescriptorResource create_sampler(VkSampler sampler);
    };

    // 描述符集
    class VulkanDescriptorSet
    {
    public:
        VulkanDescriptorSet(VkDevice device, VkDescriptorSet descriptor_set, 
                           std::shared_ptr<VulkanDescriptorSetLayout> layout);
        ~VulkanDescriptorSet() = default;

        // 更新描述符
        void update_descriptor(uint32 binding, const DescriptorResource& resource, uint32 array_index = 0);
        void update_descriptor(uint32 binding, const std::vector<DescriptorResource>& resources);
        
        // 批量更新
        void update_descriptors(const std::unordered_map<uint32, DescriptorResource>& resources);
        
        VkDescriptorSet get_descriptor_set() const { return descriptor_set; }
        std::shared_ptr<VulkanDescriptorSetLayout> get_layout() const { return layout; }

        // 标记为脏数据
        void mark_dirty() { is_dirty = true; }
        bool is_dirty_data() const { return is_dirty; }
        void clear_dirty() { is_dirty = false; }

    private:
        VkDevice device;
        VkDescriptorSet descriptor_set;
        std::shared_ptr<VulkanDescriptorSetLayout> layout;
        bool is_dirty = false;

        void write_descriptor(uint32 binding, const DescriptorResource& resource, uint32 array_index);
    };

    // 描述符池管理
    class VulkanDescriptorPool
    {
    public:
        VulkanDescriptorPool(VkDevice device, uint32 max_sets);
        ~VulkanDescriptorPool();

        VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout layout);
        void reset_pool();

        VkDescriptorPool get_pool() const { return descriptor_pool; }

    private:
        VkDevice device;
        VkDescriptorPool descriptor_pool;
        uint32 max_sets;
        uint32 allocated_sets = 0;

        void create_pool();
    };

    // 描述符集管理器
    class VulkanDescriptorSetManager
    {
    public:
        VulkanDescriptorSetManager(VulkanContext* context);
        ~VulkanDescriptorSetManager();

        // 创建描述符集布局
        std::shared_ptr<VulkanDescriptorSetLayout> create_descriptor_set_layout(
            const std::vector<DescriptorBinding>& bindings);

        // 获取或创建描述符集布局（基于hash缓存）
        std::shared_ptr<VulkanDescriptorSetLayout> get_or_create_layout(
            const std::vector<DescriptorBinding>& bindings);

        // 分配描述符集
        std::unique_ptr<VulkanDescriptorSet> allocate_descriptor_set(
            std::shared_ptr<VulkanDescriptorSetLayout> layout);

        // 创建常用的描述符集布局
        std::shared_ptr<VulkanDescriptorSetLayout> create_ubo_layout(
            uint32 binding, VkShaderStageFlags stages);
        
        std::shared_ptr<VulkanDescriptorSetLayout> create_texture_layout(
            uint32 binding, VkShaderStageFlags stages);

        std::shared_ptr<VulkanDescriptorSetLayout> create_combined_layout(
            const std::vector<std::pair<uint32, EDescriptorType>>& descriptor_types,
            VkShaderStageFlags stages);

        // 重置所有描述符池
        void reset_all_pools();

        // 帧开始时的清理工作
        void begin_frame();

    private:
        VulkanContext* context;
        VkDevice device;

        // 描述符集布局缓存（基于hash）
        std::unordered_map<uint32, std::shared_ptr<VulkanDescriptorSetLayout>> layout_cache;

        // 描述符池管理
        std::vector<std::unique_ptr<VulkanDescriptorPool>> descriptor_pools;
        uint32 current_pool_index = 0;

        // 创建新的描述符池
        std::unique_ptr<VulkanDescriptorPool> create_new_pool();
        
        // 获取可用的描述符池
        VulkanDescriptorPool* get_available_pool();
    };

    // 描述符集绑定器 - 用于管线状态
    class VulkanDescriptorSetBinder
    {
    public:
        VulkanDescriptorSetBinder();
        ~VulkanDescriptorSetBinder() = default;

        // 绑定描述符集到特定set位置
        void bind_descriptor_set(uint32 set_index, VulkanDescriptorSet* descriptor_set);
        
        // 绑定到命令缓冲区
        void bind_to_command_buffer(VkCommandBuffer cmd_buffer, VkPipelineLayout pipeline_layout, 
                                   VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS);

        // 清空所有绑定
        void clear_bindings();

        // 检查是否需要重新绑定
        bool needs_rebind() const;

    private:
        struct DescriptorSetBinding
        {
            VulkanDescriptorSet* descriptor_set = nullptr;
            uint32 last_update_frame = 0;
        };

        std::unordered_map<uint32, DescriptorSetBinding> bound_sets;
        uint32 current_frame = 0;
        bool needs_rebind_flag = true;
    };

} // namespace toy3d
