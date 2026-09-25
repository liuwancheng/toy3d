#pragma once

#include "drivers/rhi/rhi_capabilities.h"
#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "drivers/rhi/rhi_viewport_context.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace toy3d
{
    class RHIGraphicsCommandContext;
    class RHIGraphicsPipelineCache;
    class RHIQueue;

    struct RHIDeviceDesc
    {
        RHISurfaceRef primary_surface;
        bool enable_validation = false;
        std::string debug_name;
    };

    RHIStatus validate_device_desc(const RHIDeviceDesc& desc);

    class RHIDevice
    {
      public:
        RHIDevice();
        virtual ~RHIDevice();

        RHIDevice(const RHIDevice&) = delete;
        RHIDevice& operator=(const RHIDevice&) = delete;

        virtual RHIStatus initialize(const RHIDeviceDesc& desc) = 0;
        RHIStatus shutdown();
        // DeviceLost invalidates the ordinary idle-wait contract. This path still
        // rejects new object creation and releases backend state, but does not
        // issue another native wait that could prevent finite process teardown.
        RHIStatus shutdown_after_device_lost();

        virtual const RHICapabilities& capabilities() const = 0;
        virtual const RHILimits& limits() const = 0;
        virtual RHIFormatCapabilities format_capabilities(PixelFormat format) const = 0;

        virtual RHIQueue& graphics_queue() = 0;

        // RenderScene creates one viewport context per presentation surface.
        // The context owns acquire, submission, presentation, and frame-local
        // recycling so those backend details do not leak into pass code.
        RHIResult<std::unique_ptr<RHIViewportContext>> create_viewport_context(const RHISurfaceRef& surface,
                                                                               const RHIViewportContextDesc& desc);

        RHIResult<RHIBufferRef> create_buffer(const RHIBufferDesc& desc, const RHIInitialData* initial_data = nullptr);

        RHIResult<RHITextureRef> create_texture(const RHITextureDesc& desc,
                                                const RHIInitialData* initial_data = nullptr);

        RHIResult<RHIReadbackRef> create_readback(const std::string& debug_name);

        RHIResult<RHIBufferViewRef> create_buffer_view(const RHIBufferRef& buffer, const RHIBufferViewDesc& desc);

        RHIResult<RHITextureViewRef> create_texture_view(const RHITextureRef& texture, const RHITextureViewDesc& desc);

        RHIResult<RHIShaderRef> create_shader(const RHIShaderDesc& desc);

        RHIResult<RHIBindingLayoutRef> create_binding_layout(const RHIBindingLayoutDesc& desc);

        RHIResult<RHISamplerRef> create_sampler(const RHISamplerDesc& desc);

        RHIResult<RHIBindingSetRef> create_binding_set(const RHIBindingSetDesc& desc);

        // Graphics pipelines additionally use the device-owned canonical
        // cache after the common creation frontend accepts the request.
        RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline(const RHIGraphicsPipelineDesc& desc);

        RHIResult<RHIGPUFenceRef> create_gpu_fence(const std::string& debug_name);

        RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context();

      protected:
        virtual RHIResult<std::unique_ptr<RHIViewportContext>> create_viewport_context_impl(
            const RHISurfaceRef& surface, const RHIViewportContextDesc& desc) = 0;
        virtual RHIResult<RHIBufferRef> create_buffer_impl(const RHIBufferDesc& desc,
                                                           const RHIInitialData* initial_data) = 0;
        virtual RHIResult<RHITextureRef> create_texture_impl(const RHITextureDesc& desc,
                                                             const RHIInitialData* initial_data) = 0;
        virtual RHIResult<RHIReadbackRef> create_readback_impl(const std::string& debug_name);
        virtual RHIResult<RHIBufferViewRef> create_buffer_view_impl(const RHIBufferRef& buffer,
                                                                    const RHIBufferViewDesc& desc) = 0;
        virtual RHIResult<RHITextureViewRef> create_texture_view_impl(const RHITextureRef& texture,
                                                                      const RHITextureViewDesc& desc) = 0;
        virtual RHIResult<RHIShaderRef> create_shader_impl(const RHIShaderDesc& desc) = 0;
        virtual RHIResult<RHIBindingLayoutRef> create_binding_layout_impl(const RHIBindingLayoutDesc& desc) = 0;
        virtual RHIResult<RHISamplerRef> create_sampler_impl(const RHISamplerDesc& desc) = 0;
        virtual RHIResult<RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const RHIGraphicsPipelineDesc& desc) = 0;
        virtual RHIResult<RHIGPUFenceRef> create_gpu_fence_impl(const std::string& debug_name) = 0;
        virtual RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> create_graphics_command_context_impl() = 0;
        virtual bool is_initialized_impl() const = 0;
        virtual RHIStatus wait_idle_before_shutdown_impl() = 0;
        virtual RHIStatus shutdown_impl() = 0;

      private:
        template <typename T> RHIResult<T> finalize_creation_result(RHIResult<T> result, const char* object_name)
        {
            if (!result)
            {
                if (result.status().code() == RHIErrorCode::DeviceLost)
                {
                    std::lock_guard<std::mutex> lock(lifecycle_mutex);
                    device_lost = true;
                }
                return result;
            }
            if (!result.value() || !result.value()->is_owned_by(*this))
            {
                return RHIResult<T>::failure(RHIErrorCode::BackendFailure,
                                             std::string("Backend returned a null or incorrectly owned ") +
                                                 object_name + ".");
            }
            return result;
        }

        class CreationScope final
        {
          public:
            explicit CreationScope(RHIDevice& owner);
            ~CreationScope();

            CreationScope(const CreationScope&) = delete;
            CreationScope& operator=(const CreationScope&) = delete;

          private:
            RHIDevice& device;
        };

        RHIStatus begin_creation();
        void end_creation();
        RHIStatus shutdown_internal(bool wait_for_idle);

        std::unique_ptr<RHIGraphicsPipelineCache> graphics_pipeline_cache;
        std::mutex lifecycle_mutex;
        std::condition_variable lifecycle_changed;
        std::uint32_t active_creations = 0;
        bool shutting_down = false;
        bool device_lost = false;
    };
} // namespace toy3d
