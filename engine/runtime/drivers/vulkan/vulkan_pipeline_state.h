#pragma once

#include "vk_com.h"
#include "rhi/rhi_legacy_resource.h"
#include "vulkan_descriptor_sets.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace toy3d
{
    class VulkanContext;
    class VulkanShader;

    // Vulkan管线状态
    class VulkanGraphicsPipelineState : public RHIGraphicsPipelineState
    {
    public:
        VulkanGraphicsPipelineState(VulkanContext* context, const GraphicsPipelineStateInitializerRHI& initializer);
        virtual ~VulkanGraphicsPipelineState();

        VkPipeline get_pipeline() const { return graphics_pipeline; }
        VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }
        VkRenderPass get_render_pass() const { return render_pass; }

        // 描述符集相关
        void set_descriptor_set(uint32 set_index, VulkanDescriptorSet* descriptor_set);
        void bind_descriptor_sets(VkCommandBuffer cmd_buffer);

        void bind_pipeline(VkCommandBuffer comd_buffer);
        
        // 获取描述符集布局
        std::shared_ptr<VulkanDescriptorSetLayout> get_descriptor_set_layout(uint32 set_index) const;

        // 禁用拷贝
        VulkanGraphicsPipelineState(const VulkanGraphicsPipelineState&) = delete;
        VulkanGraphicsPipelineState& operator=(const VulkanGraphicsPipelineState&) = delete;

    private:
        VulkanContext* context;
        VkDevice device;

        // Vulkan管线对象
        VkPipeline graphics_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        VkRenderPass render_pass = VK_NULL_HANDLE;

        // 描述符集管理
        std::vector<std::shared_ptr<VulkanDescriptorSetLayout>> descriptor_set_layouts;
        VulkanDescriptorSetBinder descriptor_binder;

        // 创建函数
        void create_descriptor_set_layouts(const GraphicsPipelineStateInitializerRHI& initializer);
        void create_pipeline_layout();
        void create_render_pass(const GraphicsPipelineStateInitializerRHI& initializer);
        void create_graphics_pipeline(const GraphicsPipelineStateInitializerRHI& initializer);
    };

    // 管线状态缓存
    class VulkanPipelineStateCache
    {
    public:
        VulkanPipelineStateCache(VulkanContext* context);
        ~VulkanPipelineStateCache();

        // 获取或创建管线状态
        std::shared_ptr<VulkanGraphicsPipelineState> get_or_create_graphics_pipeline_state(
            const GraphicsPipelineStateInitializerRHI& initializer);

        // 清理缓存
        void clear_cache();

    private:
        VulkanContext* context;
        
        // 管线状态缓存
        std::unordered_map<uint64, std::shared_ptr<VulkanGraphicsPipelineState>> pipeline_cache;

        // 计算管线状态hash
        uint64 calculate_pipeline_hash(const GraphicsPipelineStateInitializerRHI& initializer) const;
    };

    // 管理信息
    struct PipelineLayoutInfo
    {
        std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
        std::vector<VkPushConstantRange> push_constant_ranges;

        bool operator==(const PipelineLayoutInfo& other) const;
        uint32 get_hash() const;
    };

    // 管理缓存
    class VulkanPipelineLayoutCache
    {
    public:
        VulkanPipelineLayoutCache(VkDevice device);
        ~VulkanPipelineLayoutCache();

        VkPipelineLayout get_or_create_pipeline_layout(const PipelineLayoutInfo& info);
        void clear_cache();

    private:
        VkDevice device;
        std::unordered_map<uint32, VkPipelineLayout> layout_cache;
    };

    using VulkanGraphicsPipelineStateRef = std::shared_ptr<VulkanGraphicsPipelineState>;
} // namespace toy3d
