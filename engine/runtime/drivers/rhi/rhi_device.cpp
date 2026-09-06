#include "drivers/rhi/rhi_device.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_pipeline_cache.h"

#include <utility>

namespace toy3d
{
    namespace
    {
        template <typename T> RHIResult<T> failure_from_status(const RHIStatus& status)
        {
            return RHIResult<T>::failure(status.code(), status.message());
        }

        RHIStatus validate_viewport_context_desc(const RHISurfaceRef& surface, const RHIViewportContextDesc& desc)
        {
            if (!surface)
            {
                return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                          "Viewport context creation requires a surface.");
            }
            const RHIStatus surface_status = validate_surface_desc(surface->desc());
            if (!surface_status)
            {
                return surface_status;
            }
            if (desc.width == 0 || desc.height == 0 || desc.image_count < 2 || desc.format == PixelFormat::Unknown)
            {
                return RHIStatus::failure(
                    RHIErrorCode::InvalidArgument,
                    "Viewport context descriptor requires a non-zero extent, at least two images, and a format.");
            }
            return RHIStatus::success();
        }

        RHIFormatUsage required_view_format_usage(RHIResourceViewType type)
        {
            switch (type)
            {
            case RHIResourceViewType::ShaderResource:
                return RHIFormatUsage::Sampled;
            case RHIResourceViewType::UnorderedAccess:
                return RHIFormatUsage::Storage;
            case RHIResourceViewType::RenderTarget:
                return RHIFormatUsage::RenderTarget;
            case RHIResourceViewType::DepthStencil:
                return RHIFormatUsage::DepthStencil;
            }
            return RHIFormatUsage::None;
        }

        bool uses_storage_resources(const RHIBindingLayoutDesc& desc)
        {
            for (const RHIBindingLayoutEntry& entry : desc.entries)
            {
                if (entry.type == RHIResourceBindingType::StorageTexture ||
                    entry.type == RHIResourceBindingType::StorageBuffer)
                {
                    return true;
                }
            }
            return false;
        }

        bool supports_shader_stages(RHIShaderStageFlags stages, const RHICapabilities& capabilities)
        {
            if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Geometry) && !capabilities.geometry_shader)
            {
                return false;
            }
            if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Hull | RHIShaderStageFlags::Domain) &&
                !capabilities.tessellation_shader)
            {
                return false;
            }
            if (EnumHasAnyFlags(stages, RHIShaderStageFlags::Compute) && !capabilities.compute_dispatch)
            {
                return false;
            }
            return true;
        }
    } // namespace

    RHIDevice::CreationScope::CreationScope(RHIDevice& owner) : device(owner) {}

    RHIDevice::CreationScope::~CreationScope()
    {
        device.end_creation();
    }

    RHIDevice::RHIDevice() : graphics_pipeline_cache(std::make_unique<RHIGraphicsPipelineCache>()) {}

    RHIDevice::~RHIDevice() = default;

    RHIStatus RHIDevice::shutdown()
    {
        return shutdown_internal(true);
    }

    RHIStatus RHIDevice::shutdown_after_device_lost()
    {
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex);
            device_lost = true;
        }
        return shutdown_internal(false);
    }

    RHIStatus RHIDevice::shutdown_internal(bool wait_for_idle)
    {
        {
            std::unique_lock<std::mutex> lock(lifecycle_mutex);
            if (shutting_down)
            {
                return RHIStatus::failure(RHIErrorCode::NotReady, "RHI device shutdown is already in progress.");
            }
            shutting_down = true;
            lifecycle_changed.wait(lock, [this]() { return active_creations == 0; });
        }

        const RHIStatus wait_status = wait_for_idle ? wait_idle_before_shutdown_impl() : RHIStatus::success();
        graphics_pipeline_cache->clear();
        const RHIStatus shutdown_status = shutdown_impl();
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex);
            shutting_down = false;
        }
        lifecycle_changed.notify_all();
        return wait_status ? shutdown_status : wait_status;
    }

    RHIStatus validate_device_desc(const RHIDeviceDesc& desc)
    {
        if (!desc.primary_surface)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "First-phase devices require a primary presentation surface.");
        }
        return validate_surface_desc(desc.primary_surface->desc());
    }

    RHIResult<std::unique_ptr<RHIViewportContext>> RHIDevice::create_viewport_context(
        const RHISurfaceRef& surface, const RHIViewportContextDesc& desc)
    {
        const RHIStatus validation = validate_viewport_context_desc(surface, desc);
        if (!validation)
        {
            return failure_from_status<std::unique_ptr<RHIViewportContext>>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<std::unique_ptr<RHIViewportContext>>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<std::unique_ptr<RHIViewportContext>>::failure(
                RHIErrorCode::NotReady, "Viewport creation requires an initialized RHI device.");
        }
        const RHIFormatCapabilities format_support = format_capabilities(desc.format);
        if (!EnumHasAnyFlags(format_support.usage, RHIFormatUsage::RenderTarget) ||
            (format_support.supported_sample_counts & 1U) == 0)
        {
            return RHIResult<std::unique_ptr<RHIViewportContext>>::failure(
                RHIErrorCode::Unsupported, "Viewport format is not supported for presentation rendering.");
        }
        return finalize_creation_result(create_viewport_context_impl(surface, desc), "viewport context");
    }

    RHIResult<RHIBufferRef> RHIDevice::create_buffer(const RHIBufferDesc& desc, const RHIInitialData* initial_data)
    {
        const RHIStatus validation =
            initial_data != nullptr ? validate_buffer_initial_data(desc, *initial_data) : validate_buffer_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHIBufferRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIBufferRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::NotReady,
                                                    "Buffer creation requires an initialized RHI device.");
        }
        if (initial_data != nullptr)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::Unsupported,
                                                    "Buffer initial data requires an explicit command-context upload.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::UniformBuffer) &&
            desc.size > limits().max_uniform_buffer_size)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::Unsupported,
                                                    "Uniform buffer size exceeds the device limit.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::UnorderedAccess) && !capabilities().storage_resources)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::Unsupported,
                                                    "Storage buffers are not supported by this device.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::IndirectArguments) && !capabilities().indirect_draw)
        {
            return RHIResult<RHIBufferRef>::failure(RHIErrorCode::Unsupported,
                                                    "Indirect argument buffers are not supported by this device.");
        }
        return finalize_creation_result(create_buffer_impl(desc, initial_data), "buffer");
    }

    RHIResult<RHITextureRef> RHIDevice::create_texture(const RHITextureDesc& desc, const RHIInitialData* initial_data)
    {
        const RHIStatus validation =
            initial_data != nullptr ? validate_texture_initial_data(desc, *initial_data) : validate_texture_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHITextureRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHITextureRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::NotReady,
                                                     "Texture creation requires an initialized RHI device.");
        }
        if (initial_data != nullptr)
        {
            return RHIResult<RHITextureRef>::failure(
                RHIErrorCode::Unsupported, "Texture initial data requires an explicit command-context upload.");
        }
        if ((desc.dimension == RHIResourceDimension::Texture1D || desc.dimension == RHIResourceDimension::Texture2D) &&
            (desc.width > limits().max_texture_dimension_2d || desc.height > limits().max_texture_dimension_2d))
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::Unsupported,
                                                     "Texture dimensions exceed the device limit.");
        }
        if (desc.array_layers > limits().max_texture_array_layers)
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::Unsupported,
                                                     "Texture array layers exceed the device limit.");
        }
        if (EnumHasAnyFlags(desc.usage, RHIResourceUsage::UnorderedAccess) && !capabilities().storage_resources)
        {
            return RHIResult<RHITextureRef>::failure(RHIErrorCode::Unsupported,
                                                     "Storage textures are not supported by this device.");
        }
        const RHIStatus format_status = validate_texture_format_capabilities(desc, format_capabilities(desc.format));
        if (!format_status)
        {
            return failure_from_status<RHITextureRef>(format_status);
        }
        return finalize_creation_result(create_texture_impl(desc, initial_data), "texture");
    }

    RHIResult<RHIBufferViewRef> RHIDevice::create_buffer_view(const RHIBufferRef& buffer, const RHIBufferViewDesc& desc)
    {
        if (!buffer)
        {
            return RHIResult<RHIBufferViewRef>::failure(RHIErrorCode::InvalidArgument,
                                                        "Buffer view creation requires a buffer.");
        }
        const RHIStatus validation = validate_buffer_view_desc(buffer->desc(), desc);
        if (!validation)
        {
            return failure_from_status<RHIBufferViewRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIBufferViewRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIBufferViewRef>::failure(RHIErrorCode::NotReady,
                                                        "Buffer view creation requires an initialized RHI device.");
        }
        if (!buffer->is_owned_by(*this))
        {
            return RHIResult<RHIBufferViewRef>::failure(RHIErrorCode::InvalidArgument,
                                                        "Buffer view source must belong to this RHI device.");
        }
        if (desc.type == RHIResourceViewType::UnorderedAccess && !capabilities().storage_resources)
        {
            return RHIResult<RHIBufferViewRef>::failure(
                RHIErrorCode::Unsupported, "Unordered-access buffer views are not supported by this device.");
        }
        return finalize_creation_result(create_buffer_view_impl(buffer, desc), "buffer view");
    }

    RHIResult<RHITextureViewRef> RHIDevice::create_texture_view(const RHITextureRef& texture,
                                                                const RHITextureViewDesc& desc)
    {
        if (!texture)
        {
            return RHIResult<RHITextureViewRef>::failure(RHIErrorCode::InvalidArgument,
                                                         "Texture view creation requires a texture.");
        }
        const RHIStatus validation = validate_texture_view_desc(texture->desc(), desc);
        if (!validation)
        {
            return failure_from_status<RHITextureViewRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHITextureViewRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHITextureViewRef>::failure(RHIErrorCode::NotReady,
                                                         "Texture view creation requires an initialized RHI device.");
        }
        if (!texture->is_owned_by(*this))
        {
            return RHIResult<RHITextureViewRef>::failure(RHIErrorCode::InvalidArgument,
                                                         "Texture view source must belong to this RHI device.");
        }
        const RHIFormatCapabilities format_support = format_capabilities(desc.format);
        if (!EnumHasAnyFlags(format_support.usage, required_view_format_usage(desc.type)))
        {
            return RHIResult<RHITextureViewRef>::failure(RHIErrorCode::Unsupported,
                                                         "Texture view format does not support the requested usage.");
        }
        if (desc.type == RHIResourceViewType::UnorderedAccess && !capabilities().storage_resources)
        {
            return RHIResult<RHITextureViewRef>::failure(
                RHIErrorCode::Unsupported, "Unordered-access texture views are not supported by this device.");
        }
        return finalize_creation_result(create_texture_view_impl(texture, desc), "texture view");
    }

    RHIResult<RHIShaderRef> RHIDevice::create_shader(const RHIShaderDesc& desc)
    {
        const RHIStatus validation = validate_shader_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHIShaderRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIShaderRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIShaderRef>::failure(RHIErrorCode::NotReady,
                                                    "Shader creation requires an initialized RHI device.");
        }
        if ((desc.stage == RHIShaderStage::Geometry && !capabilities().geometry_shader) ||
            ((desc.stage == RHIShaderStage::Hull || desc.stage == RHIShaderStage::Domain) &&
             !capabilities().tessellation_shader) ||
            (desc.stage == RHIShaderStage::Compute && !capabilities().compute_dispatch))
        {
            return RHIResult<RHIShaderRef>::failure(RHIErrorCode::Unsupported,
                                                    "Shader stage is not supported by this device.");
        }
        return finalize_creation_result(create_shader_impl(desc), "shader");
    }

    RHIResult<RHIBindingLayoutRef> RHIDevice::create_binding_layout(const RHIBindingLayoutDesc& desc)
    {
        const RHIStatus validation = validate_binding_layout_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHIBindingLayoutRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIBindingLayoutRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIBindingLayoutRef>::failure(
                RHIErrorCode::NotReady, "Binding layout creation requires an initialized RHI device.");
        }
        for (const RHIBindingLayoutEntry& entry : desc.entries)
        {
            if (entry.slot >= limits().max_binding_slots_per_group ||
                entry.array_count > limits().max_binding_slots_per_group - entry.slot)
            {
                return RHIResult<RHIBindingLayoutRef>::failure(RHIErrorCode::Unsupported,
                                                               "Binding layout exceeds the per-group slot limit.");
            }
            if (!supports_shader_stages(entry.stages, capabilities()))
            {
                return RHIResult<RHIBindingLayoutRef>::failure(
                    RHIErrorCode::Unsupported, "Binding layout references a shader stage unsupported by this device.");
            }
        }
        if (uses_storage_resources(desc) && !capabilities().storage_resources)
        {
            return RHIResult<RHIBindingLayoutRef>::failure(RHIErrorCode::Unsupported,
                                                           "Storage bindings are not supported by this device.");
        }
        return finalize_creation_result(create_binding_layout_impl(desc), "binding layout");
    }

    RHIResult<RHISamplerRef> RHIDevice::create_sampler(const RHISamplerDesc& desc)
    {
        const RHIStatus validation = validate_sampler_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHISamplerRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHISamplerRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHISamplerRef>::failure(RHIErrorCode::NotReady,
                                                     "Sampler creation requires an initialized RHI device.");
        }
        if (desc.max_anisotropy > limits().max_sampler_anisotropy)
        {
            return RHIResult<RHISamplerRef>::failure(RHIErrorCode::Unsupported,
                                                     "Sampler anisotropy exceeds the device limit.");
        }
        return finalize_creation_result(create_sampler_impl(desc), "sampler");
    }

    RHIResult<RHIBindingSetRef> RHIDevice::create_binding_set(const RHIBindingSetDesc& desc)
    {
        const RHIStatus validation = validate_binding_set_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHIBindingSetRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIBindingSetRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::NotReady,
                                                        "Binding set creation requires an initialized RHI device.");
        }
        if (!desc.layout->is_owned_by(*this))
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument,
                                                        "Binding set layout must belong to this RHI device.");
        }
        for (const RHIBindingValue& value : desc.bindings)
        {
            const RHIObject* object = value.buffer         ? static_cast<const RHIObject*>(value.buffer.get())
                                      : value.buffer_view  ? static_cast<const RHIObject*>(value.buffer_view.get())
                                      : value.texture_view ? static_cast<const RHIObject*>(value.texture_view.get())
                                                           : static_cast<const RHIObject*>(value.sampler.get());
            if (object == nullptr || !object->is_owned_by(*this))
            {
                return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::InvalidArgument,
                                                            "Every binding set object must belong to this RHI device.");
            }
            if (value.buffer)
            {
                const std::uint64_t range =
                    value.buffer_size == 0 ? value.buffer->desc().size - value.buffer_offset : value.buffer_size;
                if (value.buffer_offset % limits().uniform_buffer_offset_alignment != 0 ||
                    range > limits().max_uniform_buffer_size)
                {
                    return RHIResult<RHIBindingSetRef>::failure(
                        RHIErrorCode::Unsupported, "Uniform-buffer binding exceeds device alignment or range limits.");
                }
            }
        }
        if (uses_storage_resources(desc.layout->desc()) && !capabilities().storage_resources)
        {
            return RHIResult<RHIBindingSetRef>::failure(RHIErrorCode::Unsupported,
                                                        "Storage bindings are not supported by this device.");
        }
        return finalize_creation_result(create_binding_set_impl(desc), "binding set");
    }

    RHIResult<RHIGraphicsPipelineRef> RHIDevice::create_graphics_pipeline(const RHIGraphicsPipelineDesc& desc)
    {
        const RHIStatus validation = validate_graphics_pipeline_desc(desc);
        if (!validation)
        {
            return failure_from_status<RHIGraphicsPipelineRef>(validation);
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIGraphicsPipelineRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::NotReady, "Graphics pipeline creation requires an initialized RHI device.");
        }
        if (!desc.vertex_shader->is_owned_by(*this) || !desc.pixel_shader->is_owned_by(*this) ||
            !desc.binding_layout->is_owned_by(*this))
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Graphics pipeline shaders and binding layout must belong to this RHI device.");
        }
        if (desc.color_attachment_count > limits().max_color_attachments ||
            desc.vertex_buffers.size() > limits().max_vertex_buffers)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::Unsupported,
                "Graphics pipeline attachment or vertex-buffer count exceeds device limits.");
        }
        for (std::uint32_t index = 0; index < desc.color_attachment_count; ++index)
        {
            const RHIFormatCapabilities format_support = format_capabilities(desc.color_formats[index]);
            if (!EnumHasAnyFlags(format_support.usage, RHIFormatUsage::RenderTarget) ||
                (format_support.supported_sample_counts & desc.sample_count) == 0)
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Graphics pipeline color format or sample count is not supported by the device.");
            }
        }
        if (desc.depth_stencil_format != PixelFormat::Unknown)
        {
            const RHIFormatCapabilities format_support = format_capabilities(desc.depth_stencil_format);
            if (!EnumHasAnyFlags(format_support.usage, RHIFormatUsage::DepthStencil) ||
                (format_support.supported_sample_counts & desc.sample_count) == 0)
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Graphics pipeline depth-stencil format or sample count is not supported by the device.");
            }
        }
        for (const RHIGraphicsPipelineDesc::VertexAttribute& attribute : desc.vertex_attributes)
        {
            if (!EnumHasAnyFlags(format_capabilities(attribute.format).usage, RHIFormatUsage::VertexBuffer))
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Graphics pipeline vertex attribute format is not supported by the device.");
            }
        }

        return graphics_pipeline_cache->get_or_create(
            desc, [this](const RHIGraphicsPipelineDesc& canonical_desc)
            { return finalize_creation_result(create_graphics_pipeline_impl(canonical_desc), "graphics pipeline"); });
    }

    RHIResult<RHIGPUFenceRef> RHIDevice::create_gpu_fence(const std::string& debug_name)
    {
        if (debug_name.empty())
        {
            return RHIResult<RHIGPUFenceRef>::failure(RHIErrorCode::InvalidArgument,
                                                      "GPU fence creation requires a debug name.");
        }
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<RHIGPUFenceRef>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIGPUFenceRef>::failure(RHIErrorCode::NotReady,
                                                      "GPU fence creation requires an initialized RHI device.");
        }
        return finalize_creation_result(create_gpu_fence_impl(debug_name), "GPU fence");
    }

    RHIResult<std::unique_ptr<RHIGraphicsCommandContext>> RHIDevice::create_graphics_command_context()
    {
        const RHIStatus creation_status = begin_creation();
        if (!creation_status)
        {
            return failure_from_status<std::unique_ptr<RHIGraphicsCommandContext>>(creation_status);
        }
        const CreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<std::unique_ptr<RHIGraphicsCommandContext>>::failure(
                RHIErrorCode::NotReady, "Graphics command-context creation requires an initialized RHI device.");
        }
        return finalize_creation_result(create_graphics_command_context_impl(), "graphics command context");
    }

    RHIStatus RHIDevice::begin_creation()
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex);
        if (shutting_down)
        {
            return RHIStatus::failure(RHIErrorCode::NotReady,
                                      "Object creation is unavailable during RHI device shutdown.");
        }
        if (device_lost)
        {
            return RHIStatus::failure(RHIErrorCode::DeviceLost,
                                      "Object creation is unavailable after the RHI device was lost.");
        }
        ++active_creations;
        return RHIStatus::success();
    }

    void RHIDevice::end_creation()
    {
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex);
            if (active_creations > 0)
            {
                --active_creations;
            }
        }
        lifecycle_changed.notify_all();
    }
} // namespace toy3d
