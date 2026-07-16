#pragma once

#include "drivers/rhi/rhi_descriptors.h"

#include <memory>
#include <string>
#include <utility>

namespace toy3d
{
    class RHIObject
    {
    public:
        explicit RHIObject(std::string debug_name = {})
            : object_debug_name(std::move(debug_name))
        {
        }

        virtual ~RHIObject() = default;

        const std::string& debug_name() const
        {
            return object_debug_name;
        }

    private:
        std::string object_debug_name;
    };

    class RHIResource : public RHIObject
    {
    public:
        using RHIObject::RHIObject;
        ~RHIResource() override = default;
    };

    class RHIBuffer : public RHIResource
    {
    public:
        explicit RHIBuffer(RHIBufferDesc desc)
            : RHIResource(desc.debug_name)
            , resource_desc(std::move(desc))
        {
        }

        const RHIBufferDesc& desc() const
        {
            return resource_desc;
        }

    private:
        RHIBufferDesc resource_desc;
    };

    class RHITexture : public RHIResource
    {
    public:
        explicit RHITexture(RHITextureDesc desc)
            : RHIResource(desc.debug_name)
            , resource_desc(std::move(desc))
        {
        }

        const RHITextureDesc& desc() const
        {
            return resource_desc;
        }

    private:
        RHITextureDesc resource_desc;
    };

    class RHITextureView : public RHIObject
    {
    public:
        RHITextureView(
            std::shared_ptr<RHITexture> texture,
            RHITextureViewDesc desc)
            : RHIObject(desc.debug_name)
            , viewed_texture(std::move(texture))
            , view_desc(std::move(desc))
        {
        }

        const std::shared_ptr<RHITexture>& texture() const
        {
            return viewed_texture;
        }

        const RHITextureViewDesc& desc() const
        {
            return view_desc;
        }

    private:
        std::shared_ptr<RHITexture> viewed_texture;
        RHITextureViewDesc view_desc;
    };

    class RHIBufferView : public RHIObject
    {
    public:
        RHIBufferView(
            std::shared_ptr<RHIBuffer> buffer,
            RHIBufferViewDesc desc)
            : RHIObject(desc.debug_name)
            , viewed_buffer(std::move(buffer))
            , view_desc(std::move(desc))
        {
        }

        const std::shared_ptr<RHIBuffer>& buffer() const
        {
            return viewed_buffer;
        }

        const RHIBufferViewDesc& desc() const
        {
            return view_desc;
        }

    private:
        std::shared_ptr<RHIBuffer> viewed_buffer;
        RHIBufferViewDesc view_desc;
    };

    class RHIShader : public RHIObject
    {
    public:
        explicit RHIShader(RHIShaderDesc desc)
            : RHIObject(desc.debug_name)
            , shader_desc(std::move(desc))
        {
        }

        const RHIShaderDesc& desc() const
        {
            return shader_desc;
        }

    private:
        RHIShaderDesc shader_desc;
    };

    class RHIBindingLayout : public RHIObject
    {
    public:
        explicit RHIBindingLayout(RHIBindingLayoutDesc desc)
            : RHIObject(desc.debug_name)
            , layout_desc(std::move(desc))
        {
        }

        const RHIBindingLayoutDesc& desc() const
        {
            return layout_desc;
        }

    private:
        RHIBindingLayoutDesc layout_desc;
    };

    class RHISampler : public RHIObject
    {
    public:
        explicit RHISampler(RHISamplerDesc desc)
            : RHIObject(desc.debug_name)
            , sampler_desc(std::move(desc))
        {
        }

        const RHISamplerDesc& desc() const
        {
            return sampler_desc;
        }

    private:
        RHISamplerDesc sampler_desc;
    };

    class RHIGraphicsPipeline : public RHIObject
    {
    public:
        explicit RHIGraphicsPipeline(RHIGraphicsPipelineDesc desc)
            : RHIObject(desc.debug_name)
            , pipeline_desc(std::move(desc))
        {
        }

        const RHIGraphicsPipelineDesc& desc() const
        {
            return pipeline_desc;
        }

    private:
        RHIGraphicsPipelineDesc pipeline_desc;
    };

    class RHIBindingSet : public RHIObject
    {
    public:
        explicit RHIBindingSet(RHIBindingSetDesc desc)
            : RHIObject(desc.debug_name)
            , binding_set_desc(std::move(desc))
        {
        }

        RHIBindingGroup group() const
        {
            return binding_set_desc.group;
        }

        const std::shared_ptr<RHIBindingLayout>& layout() const
        {
            return binding_set_desc.layout;
        }

        const RHIBindingSetDesc& desc() const
        {
            return binding_set_desc;
        }

    private:
        RHIBindingSetDesc binding_set_desc;
    };

    // A GPU fence marks an explicit point in recorded GPU work. It is intended
    // for CPU polling, such as asynchronous readback; it does not represent
    // swapchain acquire or present synchronization.
    class RHIGPUFence : public RHIObject
    {
    public:
        using RHIObject::RHIObject;
        ~RHIGPUFence() override = default;

        virtual RHIResult<bool> is_signaled() const = 0;
    };

    enum class RHISurfacePlatform : std::uint8_t
    {
        Unknown,
        Win32,
        Glfw
    };

    struct RHISurfaceDesc
    {
        RHISurfacePlatform platform = RHISurfacePlatform::Unknown;
        void* window_handle = nullptr;
        void* application_handle = nullptr;
        std::string debug_name;
    };

    // Stores platform window identity without exposing graphics-backend types.
    // Vulkan, D3D10, and D3D12 translate the opaque handles in their own
    // platform-specific surface creation code.
    class RHISurface : public RHIObject
    {
    public:
        explicit RHISurface(RHISurfaceDesc desc)
            : RHIObject(desc.debug_name)
            , surface_desc(std::move(desc))
        {
        }

        ~RHISurface() override = default;

        const RHISurfaceDesc& desc() const
        {
            return surface_desc;
        }

    private:
        RHISurfaceDesc surface_desc;
    };

    using RHIResourceRef = std::shared_ptr<RHIResource>;
    using RHIBufferRef = std::shared_ptr<RHIBuffer>;
    using RHITextureRef = std::shared_ptr<RHITexture>;
    using RHITextureViewRef = std::shared_ptr<RHITextureView>;
    using RHIBufferViewRef = std::shared_ptr<RHIBufferView>;
    using RHIShaderRef = std::shared_ptr<RHIShader>;
    using RHIBindingLayoutRef = std::shared_ptr<RHIBindingLayout>;
    using RHISamplerRef = std::shared_ptr<RHISampler>;
    using RHIGraphicsPipelineRef = std::shared_ptr<RHIGraphicsPipeline>;
    using RHIBindingSetRef = std::shared_ptr<RHIBindingSet>;
    using RHIGPUFenceRef = std::shared_ptr<RHIGPUFence>;
    using RHISurfaceRef = std::shared_ptr<RHISurface>;

    RHIStatus validate_surface_desc(const RHISurfaceDesc& desc);
}
