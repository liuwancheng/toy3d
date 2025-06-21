#include "vulkan_pipeline_state.h"
#include "vulkan_context.h"
#include "vulkan_resource.h"
#include "rhi/rhi_inilitializer.h"
#include <cassert>
#include <functional>

namespace toy3d
{
    ////////////////////////////////// VulkanGraphicsPipelineState //////////////////////////////////

    VulkanGraphicsPipelineState::VulkanGraphicsPipelineState(VulkanContext* context, const GraphicsPipelineStateInitializerRHI& initializer)
        : context(context), device(context->device)
    {
        // 按顺序创建管线组件
        create_descriptor_set_layouts(initializer);
        create_pipeline_layout();
        create_render_pass(initializer);
        create_graphics_pipeline(initializer);
    }

    VulkanGraphicsPipelineState::~VulkanGraphicsPipelineState()
    {
        if (graphics_pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, graphics_pipeline, nullptr);
        }
        if (pipeline_layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        }
        if (render_pass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, render_pass, nullptr);
        }
    }

    void VulkanGraphicsPipelineState::set_descriptor_set(uint32 set_index, VulkanDescriptorSet* descriptor_set)
    {
        descriptor_binder.bind_descriptor_set(set_index, descriptor_set);
    }

    void VulkanGraphicsPipelineState::bind_descriptor_sets(VkCommandBuffer cmd_buffer)
    {
        descriptor_binder.bind_to_command_buffer(cmd_buffer, pipeline_layout);
    }

    std::shared_ptr<VulkanDescriptorSetLayout> VulkanGraphicsPipelineState::get_descriptor_set_layout(uint32 set_index) const
    {
        if (set_index < descriptor_set_layouts.size())
        {
            return descriptor_set_layouts[set_index];
        }
        return nullptr;
    }

    void VulkanGraphicsPipelineState::create_descriptor_set_layouts(const GraphicsPipelineStateInitializerRHI& initializer)
    {
        // 从着色器反射信息中提取描述符集布局
        // 这里简化处理，实际应该从着色器字节码中反射获取
        
        // 示例：创建基本的UBO + 纹理布局
        std::vector<DescriptorBinding> set0_bindings = {
            DescriptorBinding(0, EDescriptorType::UniformBuffer, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        
        std::vector<DescriptorBinding> set1_bindings = {
            DescriptorBinding(0, EDescriptorType::CombinedImageSampler, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        descriptor_set_layouts.push_back(std::make_shared<VulkanDescriptorSetLayout>(device, set0_bindings));
        descriptor_set_layouts.push_back(std::make_shared<VulkanDescriptorSetLayout>(device, set1_bindings));
    }

    void VulkanGraphicsPipelineState::create_pipeline_layout()
    {
        std::vector<VkDescriptorSetLayout> layouts;
        for (const auto& layout : descriptor_set_layouts)
        {
            layouts.push_back(layout->get_layout());
        }

        // Push Constants (可选)
        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = 128; // 最多128字节的push constants

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = static_cast<uint32>(layouts.size());
        pipeline_layout_info.pSetLayouts = layouts.data();
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant_range;

        VkResult result = vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout);
        assert(result == VK_SUCCESS && "Failed to create pipeline layout!");
    }

    void VulkanGraphicsPipelineState::create_render_pass(const GraphicsPipelineStateInitializerRHI& initializer)
    {
        // 颜色附件
        VkAttachmentDescription color_attachment{};
        color_attachment.format = convert_pixel_format(EPixelFormat::PF_R8G8B8A8_UNORM); // 应该从初始化器获取
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // 深度附件
        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = VK_FORMAT_D32_SFLOAT;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 附件引用
        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 1;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 子通道
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

        // 子通道依赖
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};
        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = static_cast<uint32>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        VkResult result = vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass);
        assert(result == VK_SUCCESS && "Failed to create render pass!");
    }

    void VulkanGraphicsPipelineState::create_graphics_pipeline(const GraphicsPipelineStateInitializerRHI& initializer)
    {
        // 着色器阶段
        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
        
        // 顶点着色器
        if (initializer.bound_shader_state && initializer.bound_shader_state->get_vertex_shader())
        {
            VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
            vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
            // 这里需要从RHI着色器获取VkShaderModule
            // vert_shader_stage_info.module = vertex_shader_module;
            vert_shader_stage_info.pName = "main";
            shader_stages.push_back(vert_shader_stage_info);
        }

        // 片段着色器
        if (initializer.bound_shader_state && initializer.bound_shader_state->get_pixel_shader())
        {
            VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
            frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            // frag_shader_stage_info.module = fragment_shader_module;
            frag_shader_stage_info.pName = "main";
            shader_stages.push_back(frag_shader_stage_info);
        }

        // 顶点输入
        VkPipelineVertexInputStateCreateInfo vertex_input_info{};
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        
        std::vector<VkVertexInputBindingDescription> binding_descriptions;
        std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
        
        // 从顶点声明中提取绑定和属性信息
        if (initializer.vertex_declaration_rhi)
        {
            // 这里需要实现从RHI顶点声明到Vulkan格式的转换
            // 简化示例
            VkVertexInputBindingDescription binding_desc{};
            binding_desc.binding = 0;
            binding_desc.stride = sizeof(float) * 6; // 位置 + 法线
            binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            binding_descriptions.push_back(binding_desc);

            VkVertexInputAttributeDescription attr_desc{};
            attr_desc.binding = 0;
            attr_desc.location = 0;
            attr_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
            attr_desc.offset = 0;
            attribute_descriptions.push_back(attr_desc);

            attr_desc.location = 1;
            attr_desc.offset = sizeof(float) * 3;
            attribute_descriptions.push_back(attr_desc);
        }

        vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32>(binding_descriptions.size());
        vertex_input_info.pVertexBindingDescriptions = binding_descriptions.data();
        vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32>(attribute_descriptions.size());
        vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

        // 输入装配
        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        // 视口状态
        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        // 光栅化状态
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        // 从RHI光栅化状态更新
        if (initializer.rasterizer_state)
        {
            VulkanRasterizerState* vk_rasterizer = static_cast<VulkanRasterizerState*>(initializer.rasterizer_state);
            rasterizer = vk_rasterizer->rasterizer_state;
        }

        // 多重采样状态
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 深度模板状态
        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = VK_FALSE;

        // 从RHI深度模板状态更新
        if (initializer.depth_stencil_state)
        {
            VulkanDepthStencilState* vk_depth_stencil = static_cast<VulkanDepthStencilState*>(initializer.depth_stencil_state);
            depth_stencil = vk_depth_stencil->depth_stencil_state;
        }

        // 颜色混合状态
        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.logicOp = VK_LOGIC_OP_COPY;
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;

        // 从RHI混合状态更新
        if (initializer.blend_state)
        {
            VulkanBlendState* vk_blend = static_cast<VulkanBlendState*>(initializer.blend_state);
            color_blending.pAttachments = vk_blend->blend_states;
        }

        // 动态状态
        std::vector<VkDynamicState> dynamic_states = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<uint32>(dynamic_states.size());
        dynamic_state.pDynamicStates = dynamic_states.data();

        // 创建图形管线
        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = static_cast<uint32>(shader_stages.size());
        pipeline_info.pStages = shader_stages.data();
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline);
        assert(result == VK_SUCCESS && "Failed to create graphics pipeline!");
    }

    // 格式转换辅助函数
    VkFormat VulkanGraphicsPipelineState::convert_pixel_format(EPixelFormat format) const
    {
        switch (format)
        {
            case EPixelFormat::PF_R8G8B8A8_UNORM:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case EPixelFormat::PF_B8G8R8A8_UNORM:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case EPixelFormat::PF_R32G32B32A32_SFLOAT:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case EPixelFormat::PF_D32_SFLOAT:
                return VK_FORMAT_D32_SFLOAT;
            case EPixelFormat::PF_D24_UNORM_S8_UINT:
                return VK_FORMAT_D24_UNORM_S8_UINT;
            default:
                return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    VkCompareOp VulkanGraphicsPipelineState::convert_compare_function(ECompareFunction func) const
    {
        switch (func)
        {
            case CF_Never:
                return VK_COMPARE_OP_NEVER;
            case CF_Less:
                return VK_COMPARE_OP_LESS;
            case CF_Equal:
                return VK_COMPARE_OP_EQUAL;
            case CF_LessEqual:
                return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CF_Greater:
                return VK_COMPARE_OP_GREATER;
            case CF_NotEqual:
                return VK_COMPARE_OP_NOT_EQUAL;
            case CF_GreaterEqual:
                return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CF_Always:
                return VK_COMPARE_OP_ALWAYS;
            default:
                return VK_COMPARE_OP_LESS;
        }
    }

    VkBlendFactor VulkanGraphicsPipelineState::convert_blend_factor(EBlendFactor factor) const
    {
        switch (factor)
        {
            case BF_Zero:
                return VK_BLEND_FACTOR_ZERO;
            case BF_One:
                return VK_BLEND_FACTOR_ONE;
            case BF_SourceColor:
                return VK_BLEND_FACTOR_SRC_COLOR;
            case BF_InverseSourceColor:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BF_SourceAlpha:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case BF_InverseSourceAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BF_DestAlpha:
                return VK_BLEND_FACTOR_DST_ALPHA;
            case BF_InverseDestAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BF_DestColor:
                return VK_BLEND_FACTOR_DST_COLOR;
            case BF_InverseDestColor:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            default:
                return VK_BLEND_FACTOR_ONE;
        }
    }

    VkBlendOp VulkanGraphicsPipelineState::convert_blend_op(EBlendOperation op) const
    {
        switch (op)
        {
            case BO_Add:
                return VK_BLEND_OP_ADD;
            case BO_Subtract:
                return VK_BLEND_OP_SUBTRACT;
            case BO_Min:
                return VK_BLEND_OP_MIN;
            case BO_Max:
                return VK_BLEND_OP_MAX;
            case BO_ReverseSubtract:
                return VK_BLEND_OP_REVERSE_SUBTRACT;
            default:
                return VK_BLEND_OP_ADD;
        }
    }

    VkCullModeFlags VulkanGraphicsPipelineState::convert_cull_mode(ERasterizerCullMode mode) const
    {
        switch (mode)
        {
            case CM_None:
                return VK_CULL_MODE_NONE;
            case CM_CW:
                return VK_CULL_MODE_FRONT_BIT;
            case CM_CCW:
                return VK_CULL_MODE_BACK_BIT;
            default:
                return VK_CULL_MODE_BACK_BIT;
        }
    }

    VkPolygonMode VulkanGraphicsPipelineState::convert_fill_mode(ERasterizerFillMode mode) const
    {
        switch (mode)
        {
            case FM_Point:
                return VK_POLYGON_MODE_POINT;
            case FM_Wireframe:
                return VK_POLYGON_MODE_LINE;
            case FM_Solid:
                return VK_POLYGON_MODE_FILL;
            default:
                return VK_POLYGON_MODE_FILL;
        }
    }

    ////////////////////////////////// VulkanPipelineStateCache //////////////////////////////////

    VulkanPipelineStateCache::VulkanPipelineStateCache(VulkanContext* context)
        : context(context)
    {
    }

    VulkanPipelineStateCache::~VulkanPipelineStateCache()
    {
        clear_cache();
    }

    std::shared_ptr<VulkanGraphicsPipelineState> VulkanPipelineStateCache::get_or_create_graphics_pipeline_state(
        const GraphicsPipelineStateInitializerRHI& initializer)
    {
        uint64 hash = calculate_pipeline_hash(initializer);
        
        auto it = pipeline_cache.find(hash);
        if (it != pipeline_cache.end())
        {
            return it->second;
        }

        auto pipeline_state = std::make_shared<VulkanGraphicsPipelineState>(context, initializer);
        pipeline_cache[hash] = pipeline_state;
        return pipeline_state;
    }

    void VulkanPipelineStateCache::clear_cache()
    {
        pipeline_cache.clear();
    }

    uint64 VulkanPipelineStateCache::calculate_pipeline_hash(const GraphicsPipelineStateInitializerRHI& initializer) const
    {
        // 简化的hash计算，实际应该包含所有相关状态
        size_t hash = 0;
        
        // 着色器状态hash
        if (initializer.bound_shader_state)
        {
            hash ^= std::hash<void*>{}(initializer.bound_shader_state) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        // 光栅化状态hash
        if (initializer.rasterizer_state)
        {
            hash ^= std::hash<void*>{}(initializer.rasterizer_state) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        // 深度模板状态hash
        if (initializer.depth_stencil_state)
        {
            hash ^= std::hash<void*>{}(initializer.depth_stencil_state) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        // 混合状态hash
        if (initializer.blend_state)
        {
            hash ^= std::hash<void*>{}(initializer.blend_state) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        // 顶点声明hash
        if (initializer.vertex_declaration_rhi)
        {
            hash ^= std::hash<void*>{}(initializer.vertex_declaration_rhi) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }

        return static_cast<uint64>(hash);
    }

    ////////////////////////////////// PipelineLayoutInfo //////////////////////////////////

    bool PipelineLayoutInfo::operator==(const PipelineLayoutInfo& other) const
    {
        if (descriptor_set_layouts.size() != other.descriptor_set_layouts.size() ||
            push_constant_ranges.size() != other.push_constant_ranges.size())
        {
            return false;
        }

        for (size_t i = 0; i < descriptor_set_layouts.size(); ++i)
        {
            if (descriptor_set_layouts[i] != other.descriptor_set_layouts[i])
            {
                return false;
            }
        }

        for (size_t i = 0; i < push_constant_ranges.size(); ++i)
        {
            const auto& a = push_constant_ranges[i];
            const auto& b = other.push_constant_ranges[i];
            if (a.stageFlags != b.stageFlags || a.offset != b.offset || a.size != b.size)
            {
                return false;
            }
        }

        return true;
    }

    uint32 PipelineLayoutInfo::get_hash() const
    {
        size_t hash = 0;
        
        for (const auto& layout : descriptor_set_layouts)
        {
            hash ^= std::hash<VkDescriptorSetLayout>{}(layout) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        for (const auto& range : push_constant_ranges)
        {
            hash ^= std::hash<uint32>{}(range.stageFlags) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(range.offset) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(range.size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        return static_cast<uint32>(hash);
    }

    ////////////////////////////////// VulkanPipelineLayoutCache //////////////////////////////////

    VulkanPipelineLayoutCache::VulkanPipelineLayoutCache(VkDevice device)
        : device(device)
    {
    }

    VulkanPipelineLayoutCache::~VulkanPipelineLayoutCache()
    {
        clear_cache();
    }

    VkPipelineLayout VulkanPipelineLayoutCache::get_or_create_pipeline_layout(const PipelineLayoutInfo& info)
    {
        uint32 hash = info.get_hash();
        
        auto it = layout_cache.find(hash);
        if (it != layout_cache.end())
        {
            return it->second;
        }

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = static_cast<uint32>(info.descriptor_set_layouts.size());
        layout_info.pSetLayouts = info.descriptor_set_layouts.data();
        layout_info.pushConstantRangeCount = static_cast<uint32>(info.push_constant_ranges.size());
        layout_info.pPushConstantRanges = info.push_constant_ranges.data();

        VkPipelineLayout pipeline_layout;
        VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout);
        assert(result == VK_SUCCESS && "Failed to create pipeline layout!");

        layout_cache[hash] = pipeline_layout;
        return pipeline_layout;
    }

    void VulkanPipelineLayoutCache::clear_cache()
    {
        for (auto& [hash, layout] : layout_cache)
        {
            vkDestroyPipelineLayout(device, layout, nullptr);
        }
        layout_cache.clear();
    }

} // namespace toy3d