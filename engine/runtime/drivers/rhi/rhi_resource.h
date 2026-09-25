#pragma once

#include "drivers/rhi/rhi_descriptors.h"

#include <memory>
#include <atomic>
#include <string>
#include <utility>

namespace toy3d
{
    class RHIDevice;

    class RHIObject
    {
      public:
        explicit RHIObject(std::string debug_name = {}) : RHIObject(nullptr, std::move(debug_name)) {}

        RHIObject(const RHIDevice& owner, std::string debug_name = {}) : RHIObject(&owner, std::move(debug_name)) {}

        virtual ~RHIObject() = default;

        const std::string& debug_name() const { return object_debug_name; }

        const RHIDevice* owner_device() const { return owning_device; }

        bool is_owned_by(const RHIDevice& device) const { return owning_device == &device; }

      protected:
        RHIObject(const RHIDevice* owner, std::string debug_name)
            : owning_device(owner), object_debug_name(std::move(debug_name))
        {
        }

      private:
        // The creating device address is stable for the device lifetime and
        // cannot be replaced after construction. Null is reserved for
        // platform surfaces and descriptor-only test objects created before a
        // device exists.
        const RHIDevice* const owning_device = nullptr;
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
        explicit RHIBuffer(RHIBufferDesc desc) : RHIResource(desc.debug_name), resource_desc(std::move(desc)) {}

        RHIBuffer(const RHIDevice& owner, RHIBufferDesc desc)
            : RHIResource(owner, desc.debug_name), resource_desc(std::move(desc))
        {
        }

        const RHIBufferDesc& desc() const { return resource_desc; }

      private:
        RHIBufferDesc resource_desc;
    };

    // A single R32UInt pixel result. The backend chooses its native staging
    // resource; the caller only polls after the owning queue has completed.
    class RHIReadback : public RHIResource
    {
      public:
        explicit RHIReadback(const RHIDevice& owner, std::string debug_name = {})
            : RHIResource(owner, std::move(debug_name))
        {
        }

        RHIResult<std::uint32_t> read_uint32(RHIQueueCompletionValue completed_value) const;
        void mark_used(RHIQueueCompletionValue completion_value);
        RHIQueueCompletionValue last_use_completion_value() const;

      protected:
        virtual RHIResult<std::uint32_t> read_uint32_impl() const;

      private:
        std::atomic<RHIQueueCompletionValue> last_use_value{0};
    };

    class RHITexture : public RHIResource
    {
      public:
        explicit RHITexture(RHITextureDesc desc) : RHIResource(desc.debug_name), resource_desc(std::move(desc)) {}

        RHITexture(const RHIDevice& owner, RHITextureDesc desc)
            : RHIResource(owner, desc.debug_name), resource_desc(std::move(desc))
        {
        }

        const RHITextureDesc& desc() const { return resource_desc; }

      private:
        RHITextureDesc resource_desc;
    };

    class RHITextureView : public RHIObject
    {
      public:
        RHITextureView(std::shared_ptr<RHITexture> texture, RHITextureViewDesc desc)
            : RHIObject(texture ? texture->owner_device() : nullptr, desc.debug_name),
              viewed_texture(std::move(texture)), view_desc(std::move(desc))
        {
        }

        const std::shared_ptr<RHITexture>& texture() const { return viewed_texture; }

        const RHITextureViewDesc& desc() const { return view_desc; }

      private:
        std::shared_ptr<RHITexture> viewed_texture;
        RHITextureViewDesc view_desc;
    };

    class RHIBufferView : public RHIObject
    {
      public:
        RHIBufferView(std::shared_ptr<RHIBuffer> buffer, RHIBufferViewDesc desc)
            : RHIObject(buffer ? buffer->owner_device() : nullptr, desc.debug_name), viewed_buffer(std::move(buffer)),
              view_desc(std::move(desc))
        {
        }

        const std::shared_ptr<RHIBuffer>& buffer() const { return viewed_buffer; }

        const RHIBufferViewDesc& desc() const { return view_desc; }

      private:
        std::shared_ptr<RHIBuffer> viewed_buffer;
        RHIBufferViewDesc view_desc;
    };

    class RHIShader : public RHIObject
    {
      public:
        explicit RHIShader(RHIShaderDesc desc) : RHIObject(desc.debug_name), shader_desc(std::move(desc)) {}

        RHIShader(const RHIDevice& owner, RHIShaderDesc desc)
            : RHIObject(owner, desc.debug_name), shader_desc(std::move(desc))
        {
        }

        const RHIShaderDesc& desc() const { return shader_desc; }

      private:
        RHIShaderDesc shader_desc;
    };

    class RHIBindingLayout : public RHIObject
    {
      public:
        explicit RHIBindingLayout(RHIBindingLayoutDesc desc) : RHIObject(desc.debug_name), layout_desc(std::move(desc))
        {
        }

        RHIBindingLayout(const RHIDevice& owner, RHIBindingLayoutDesc desc)
            : RHIObject(owner, desc.debug_name), layout_desc(std::move(desc))
        {
        }

        const RHIBindingLayoutDesc& desc() const { return layout_desc; }

      private:
        RHIBindingLayoutDesc layout_desc;
    };

    class RHISampler : public RHIObject
    {
      public:
        explicit RHISampler(RHISamplerDesc desc) : RHIObject(desc.debug_name), sampler_desc(std::move(desc)) {}

        RHISampler(const RHIDevice& owner, RHISamplerDesc desc)
            : RHIObject(owner, desc.debug_name), sampler_desc(std::move(desc))
        {
        }

        const RHISamplerDesc& desc() const { return sampler_desc; }

      private:
        RHISamplerDesc sampler_desc;
    };

    class RHIGraphicsPipeline : public RHIObject
    {
      public:
        explicit RHIGraphicsPipeline(RHIGraphicsPipelineDesc desc)
            : RHIObject(desc.debug_name), pipeline_desc(std::move(desc))
        {
        }

        RHIGraphicsPipeline(const RHIDevice& owner, RHIGraphicsPipelineDesc desc)
            : RHIObject(owner, desc.debug_name), pipeline_desc(std::move(desc))
        {
        }

        const RHIGraphicsPipelineDesc& desc() const { return pipeline_desc; }

      private:
        RHIGraphicsPipelineDesc pipeline_desc;
    };

    class RHIBindingSet : public RHIObject
    {
      public:
        RHIBindingSet(const RHIDevice& owner, RHIBindingSetDesc desc)
            : RHIObject(owner, desc.debug_name), binding_set_desc(std::move(desc))
        {
        }

        RHIBindingGroup group() const { return binding_set_desc.group; }

        const RHIBindingSetDesc& desc() const { return binding_set_desc; }

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
        MacOS,
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
    // Vulkan, D3D11, and D3D12 translate the opaque handles in their own
    // platform-specific surface creation code.
    class RHISurface : public RHIObject
    {
      public:
        explicit RHISurface(RHISurfaceDesc desc) : RHIObject(desc.debug_name), surface_desc(std::move(desc)) {}

        ~RHISurface() override = default;

        const RHISurfaceDesc& desc() const { return surface_desc; }

      private:
        RHISurfaceDesc surface_desc;
    };

    using RHIResourceRef = std::shared_ptr<RHIResource>;
    using RHIBufferRef = std::shared_ptr<RHIBuffer>;
    using RHIReadbackRef = std::shared_ptr<RHIReadback>;
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
} // namespace toy3d
