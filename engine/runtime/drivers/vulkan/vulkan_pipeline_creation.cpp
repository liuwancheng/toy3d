#include "drivers/vulkan/vulkan_pipeline_creation.h"

#include "drivers/vulkan/vulkan_resource.h"
#include "drivers/vulkan/vulkan_type_mapping.h"

#include <iterator>
#include <vector>

namespace toy3d
{
    RHIResult<RHIGraphicsPipelineRef> create_vulkan_graphics_pipeline(
        const RHIDevice& owner,
        VkDevice device,
        const RHIGraphicsPipelineDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::NotReady,
                "Vulkan graphics-pipeline creation requires a valid logical device.");
        }
        const auto vertex_shader = std::dynamic_pointer_cast<VulkanShader>(desc.vertex_shader);
        const auto pixel_shader = std::dynamic_pointer_cast<VulkanShader>(desc.pixel_shader);
        const auto binding_layout = std::dynamic_pointer_cast<VulkanBindingLayout>(desc.binding_layout);
        if (!vertex_shader || !pixel_shader || !binding_layout)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics pipelines require shaders and a binding layout created by the Vulkan device.");
        }
        if (desc.color_attachment_count == 0 && desc.depth_stencil_format == PixelFormat::Unknown)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Vulkan graphics pipelines require at least one color or depth-stencil attachment.");
        }
        if (desc.rasterization.depth_clamp_enable || desc.rasterization.polygon_mode != RHIPolygonMode::Fill)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::Unsupported,
                "Vulkan graphics pipelines currently support filled primitives without depth clamp only.");
        }

        const auto primitive_topology = to_vk_primitive_topology(desc.primitive_topology);
        const auto cull_mode = to_vk_cull_mode(desc.rasterization.cull_mode);
        const auto front_face = to_vk_front_face(desc.rasterization.front_face);
        const auto sample_count = to_vk_sample_count(desc.sample_count);
        if (!primitive_topology || !cull_mode || !front_face || !sample_count)
        {
            const RHIStatus& status = !primitive_topology ? primitive_topology.status() :
                (!cull_mode ? cull_mode.status() : (!front_face ? front_face.status() : sample_count.status()));
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }

        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> attachment_references;
        std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
        attachments.reserve(desc.color_attachment_count +
            (desc.depth_stencil_format != PixelFormat::Unknown ? 1U : 0U));
        attachment_references.reserve(desc.color_attachment_count);
        blend_attachments.reserve(desc.color_attachment_count);
        for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
        {
            const VkFormat format = vulkan_format_from_pixel_format(desc.color_formats[index]);
            if (format == VK_FORMAT_UNDEFINED)
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "A Vulkan graphics pipeline color attachment format has no Vulkan mapping.");
            }
            VkAttachmentDescription attachment{};
            attachment.format = format;
            attachment.samples = sample_count.value();
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments.push_back(attachment);
            attachment_references.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

            const auto& blend = desc.color_blend_attachments[index];
            const auto source_color = to_vk_blend_factor(blend.source_color_factor);
            const auto destination_color = to_vk_blend_factor(blend.destination_color_factor);
            const auto source_alpha = to_vk_blend_factor(blend.source_alpha_factor);
            const auto destination_alpha = to_vk_blend_factor(blend.destination_alpha_factor);
            const auto color_operation = to_vk_blend_operation(blend.color_operation);
            const auto alpha_operation = to_vk_blend_operation(blend.alpha_operation);
            if (!source_color || !destination_color || !source_alpha || !destination_alpha ||
                !color_operation || !alpha_operation)
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan graphics pipeline has an invalid color blend state.");
            }
            VkPipelineColorBlendAttachmentState vk_blend{};
            vk_blend.blendEnable = blend.blend_enable ? VK_TRUE : VK_FALSE;
            vk_blend.srcColorBlendFactor = source_color.value();
            vk_blend.dstColorBlendFactor = destination_color.value();
            vk_blend.colorBlendOp = color_operation.value();
            vk_blend.srcAlphaBlendFactor = source_alpha.value();
            vk_blend.dstAlphaBlendFactor = destination_alpha.value();
            vk_blend.alphaBlendOp = alpha_operation.value();
            vk_blend.colorWriteMask = to_vk_color_write_mask(blend.color_write_mask);
            blend_attachments.push_back(vk_blend);
        }

        VkAttachmentReference depth_stencil_reference{};
        const bool has_depth_stencil_attachment = desc.depth_stencil_format != PixelFormat::Unknown;
        if (has_depth_stencil_attachment)
        {
            const VkFormat format = vulkan_format_from_pixel_format(desc.depth_stencil_format);
            if (!is_vk_depth_format(format))
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "The Vulkan graphics pipeline depth-stencil format is not a supported depth format.");
            }
            if (desc.depth_stencil.stencil_test_enable && !is_vk_stencil_format(format))
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::InvalidArgument,
                    "Vulkan stencil testing requires a depth-stencil format with a stencil aspect.");
            }
            VkAttachmentDescription attachment{};
            attachment.format = format;
            attachment.samples = sample_count.value();
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth_stencil_reference.attachment = static_cast<std::uint32_t>(attachments.size());
            depth_stencil_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments.push_back(attachment);
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(attachment_references.size());
        subpass.pColorAttachments = attachment_references.empty()
            ? nullptr
            : attachment_references.data();
        subpass.pDepthStencilAttachment = has_depth_stencil_attachment
            ? &depth_stencil_reference
            : nullptr;
        VkRenderPassCreateInfo render_pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render_pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        VkRenderPass compatibility_render_pass = VK_NULL_HANDLE;
        RHIStatus status = vulkan_status_from_result(
            vkCreateRenderPass(device, &render_pass_info, nullptr, &compatibility_render_pass),
            "vkCreateRenderPass for graphics pipeline");
        if (!status)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }

        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        const auto& descriptor_set_layouts = binding_layout->descriptor_set_layouts();
        layout_info.setLayoutCount = static_cast<std::uint32_t>(descriptor_set_layouts.size());
        layout_info.pSetLayouts = descriptor_set_layouts.data();
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        status = vulkan_status_from_result(
            vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout),
            "vkCreatePipelineLayout");
        if (!status)
        {
            vkDestroyRenderPass(device, compatibility_render_pass, nullptr);
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }

        std::vector<VkVertexInputBindingDescription> vertex_bindings;
        std::vector<VkVertexInputAttributeDescription> vertex_attributes;
        vertex_bindings.reserve(desc.vertex_buffers.size());
        vertex_attributes.reserve(desc.vertex_attributes.size());
        for (const auto& layout : desc.vertex_buffers)
        {
            const auto input_rate = to_vk_vertex_input_rate(layout.input_rate);
            if (!input_rate)
            {
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                vkDestroyRenderPass(device, compatibility_render_pass, nullptr);
                return RHIResult<RHIGraphicsPipelineRef>::failure(input_rate.status().code(), input_rate.status().message());
            }
            vertex_bindings.push_back({layout.binding, layout.stride, input_rate.value()});
        }
        for (const auto& attribute : desc.vertex_attributes)
        {
            const VkFormat format = vulkan_format_from_pixel_format(attribute.format);
            if (format == VK_FORMAT_UNDEFINED)
            {
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                vkDestroyRenderPass(device, compatibility_render_pass, nullptr);
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "A Vulkan graphics pipeline vertex attribute format has no Vulkan mapping.");
            }
            // Public validation has already matched this location/format to
            // vertex-shader reflection. Vulkan consumes the location directly;
            // semantic name/index remain available for D3D backends only.
            vertex_attributes.push_back({attribute.location, attribute.binding, format, attribute.offset});
        }

        VkPipelineShaderStageCreateInfo shader_stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shader_stages[0].module = vertex_shader->shader_module();
        shader_stages[0].pName = desc.vertex_shader->desc().entry_point.c_str();
        shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shader_stages[1].module = pixel_shader->shader_module();
        shader_stages[1].pName = desc.pixel_shader->desc().entry_point.c_str();
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex_input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertex_bindings.size());
        vertex_input.pVertexBindingDescriptions = vertex_bindings.data();
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertex_attributes.size());
        vertex_input.pVertexAttributeDescriptions = vertex_attributes.data();
        VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = primitive_topology.value();
        VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = cull_mode.value();
        rasterizer.frontFace = front_face.value();
        rasterizer.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = sample_count.value();
        VkPipelineDepthStencilStateCreateInfo depth_stencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_stencil.depthTestEnable = desc.depth_stencil.depth_test_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.depthWriteEnable = desc.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.depthCompareOp = to_vk_compare_operation(desc.depth_stencil.depth_compare_operation);
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = desc.depth_stencil.stencil_test_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.front = to_vk_stencil_face(
            desc.depth_stencil.front_face,
            desc.depth_stencil.stencil_read_mask,
            desc.depth_stencil.stencil_write_mask);
        depth_stencil.back = to_vk_stencil_face(
            desc.depth_stencil.back_face,
            desc.depth_stencil.stencil_read_mask,
            desc.depth_stencil.stencil_write_mask);
        VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        color_blend.attachmentCount = static_cast<std::uint32_t>(blend_attachments.size());
        color_blend.pAttachments = blend_attachments.empty() ? nullptr : blend_attachments.data();
        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo dynamic_state{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        // std::size derives the native C-array length so the Vulkan count cannot
        // drift when dynamic states are added or removed.
        dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
        dynamic_state.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = has_depth_stencil_attachment ? &depth_stencil : nullptr;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = compatibility_render_pass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        status = vulkan_status_from_result(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline),
            "vkCreateGraphicsPipelines");
        if (!status)
        {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            vkDestroyRenderPass(device, compatibility_render_pass, nullptr);
            return RHIResult<RHIGraphicsPipelineRef>::failure(status.code(), status.message());
        }
        return RHIResult<RHIGraphicsPipelineRef>::success(std::make_shared<VulkanGraphicsPipeline>(
            owner,
            desc,
            device,
            compatibility_render_pass,
            pipeline_layout,
            pipeline));
    }
}
