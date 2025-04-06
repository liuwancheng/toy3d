#pragma once

#include "vk_com.h"
#include "rhi/rhi.h"
#include "vulkan_context.h"

class IWindow;

namespace toy3d
{
    class VulkanDynamicRHI : public IDynamicRHI
    {
    public:
        VulkanDynamicRHI() = default;
        ~VulkanDynamicRHI() = default;

        virtual void init() override;

        virtual void clear() override;

        virtual void begin_frame() override;

        virtual void end_frame() override;

        virtual void begin_render_pass(const RHIRenderPassInfo &info, std::string pass_name) override;

        virtual void end_render_pass() override;

    private:
        VkAttachmentDescription cast_vk_attachment_desc(const RHIRenderPassInfo &info);

    private:
        std::unique_ptr<VulkanContext> m_context = nullptr;

        RHIRenderPassInfo cache_pass_info;
    };
}