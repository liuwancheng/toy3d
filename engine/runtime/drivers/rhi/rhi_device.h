#pragma once

#include "drivers/rhi/rhi_capabilities.h"
#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"

#include <memory>

namespace toy3d
{
    class RHIGraphicsCommandContext;
    class RHIQueue;
    class RHISwapchain;
    struct RHISwapchainDesc;

    class RHIDevice
    {
    public:
        RHIDevice() = default;
        virtual ~RHIDevice() = default;

        RHIDevice(const RHIDevice&) = delete;
        RHIDevice& operator=(const RHIDevice&) = delete;

        virtual RHIStatus initialize() = 0;
        virtual RHIStatus shutdown() = 0;

        virtual const RHICapabilities& capabilities() const = 0;
        virtual const RHILimits& limits() const = 0;
        virtual RHIFormatCapabilities format_capabilities(RHIFormat format) const = 0;

        virtual RHIQueue& graphics_queue() = 0;

        virtual RHIResult<std::shared_ptr<RHISwapchain>> create_swapchain(
            const RHISurfaceRef& surface,
            const RHISwapchainDesc& desc) = 0;

        virtual RHIResult<RHIBufferRef> create_buffer(
            const RHIBufferDesc& desc,
            const RHIInitialData* initial_data = nullptr) = 0;

        virtual RHIResult<RHITextureRef> create_texture(
            const RHITextureDesc& desc,
            const RHIInitialData* initial_data = nullptr) = 0;

        virtual RHIResult<RHIBufferViewRef> create_buffer_view(
            const RHIBufferRef& buffer,
            const RHIBufferViewDesc& desc) = 0;

        virtual RHIResult<RHITextureViewRef> create_texture_view(
            const RHITextureRef& texture,
            const RHITextureViewDesc& desc) = 0;

        virtual RHIResult<RHIShaderRef> create_shader(
            const RHIShaderDesc& desc) = 0;

        virtual RHIResult<RHIBindingLayoutRef> create_binding_layout(
            const RHIBindingLayoutDesc& desc) = 0;

        virtual RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline(
            const RHIGraphicsPipelineDesc& desc) = 0;

        virtual RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>
            create_graphics_command_context() = 0;
    };
}
