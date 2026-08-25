#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_pipeline_cache.h"

namespace toy3d
{
    RHIResult<RHIShaderRef> RHIDevice::create_shader(const RHIShaderDesc& desc)
    {
        const RHIStatus validation = validate_shader_desc(desc);
        if (!validation)
        {
            return RHIResult<RHIShaderRef>::failure(
                validation.code(), validation.message());
        }
        if (!is_initialized_impl())
        {
            return RHIResult<RHIShaderRef>::failure(
                RHIErrorCode::NotReady, "RHI device is not initialized.");
        }
        return create_shader_impl(desc);
    }

    RHIResult<RHIBindingLayoutRef> RHIDevice::create_binding_layout(
        const RHIBindingLayoutDesc& desc)
    {
        const RHIStatus validation = validate_binding_layout_desc(desc);
        if (!validation)
        {
            return RHIResult<RHIBindingLayoutRef>::failure(
                validation.code(), validation.message());
        }
        if (!is_initialized_impl())
        {
            return RHIResult<RHIBindingLayoutRef>::failure(
                RHIErrorCode::NotReady, "RHI device is not initialized.");
        }
        return create_binding_layout_impl(desc);
    }

    RHIDevice::PipelineCreationScope::PipelineCreationScope(RHIDevice& owner)
        : device(owner)
    {
    }

    RHIDevice::PipelineCreationScope::~PipelineCreationScope()
    {
        device.end_pipeline_creation();
    }

    RHIDevice::RHIDevice()
        : graphics_pipeline_cache(std::make_unique<RHIGraphicsPipelineCache>())
    {
    }

    RHIDevice::~RHIDevice() = default;

    RHIStatus RHIDevice::shutdown()
    {
        return shutdown_internal(true);
    }

    RHIStatus RHIDevice::shutdown_after_device_lost()
    {
        return shutdown_internal(false);
    }

    RHIStatus RHIDevice::shutdown_internal(bool wait_for_idle)
    {
        {
            std::unique_lock<std::mutex> lock(lifecycle_mutex);
            if (shutting_down)
            {
                return RHIStatus::failure(
                    RHIErrorCode::NotReady,
                    "RHI device shutdown is already in progress.");
            }
            shutting_down = true;
            lifecycle_changed.wait(lock, [this]()
            {
                return active_pipeline_creations == 0;
            });
        }

        const RHIStatus wait_status = wait_for_idle
            ? wait_idle_before_shutdown_impl()
            : RHIStatus::success();
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
            return RHIStatus::failure(
                RHIErrorCode::InvalidArgument,
                "First-phase devices require a primary presentation surface.");
        }
        return validate_surface_desc(desc.primary_surface->desc());
    }

    RHIResult<RHIGraphicsPipelineRef> RHIDevice::create_graphics_pipeline(
        const RHIGraphicsPipelineDesc& desc)
    {
        const RHIStatus validation = validate_graphics_pipeline_desc(desc);
        if (!validation)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                validation.code(), validation.message());
        }
        const RHIStatus creation_status = begin_pipeline_creation();
        if (!creation_status)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                creation_status.code(), creation_status.message());
        }
        const PipelineCreationScope creation_scope(*this);
        if (!is_initialized_impl())
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::NotReady,
                "Graphics pipeline creation requires an initialized RHI device.");
        }
        if (!desc.vertex_shader->is_owned_by(*this) ||
            !desc.pixel_shader->is_owned_by(*this) ||
            !desc.binding_layout->is_owned_by(*this))
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::InvalidArgument,
                "Graphics pipeline shaders and binding layout must belong to this RHI device.");
        }
        if (desc.color_attachment_count > limits().max_color_attachments)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::Unsupported,
                "Graphics pipeline color attachment count exceeds the device limit.");
        }
        if (desc.vertex_buffers.size() > limits().max_vertex_buffers)
        {
            return RHIResult<RHIGraphicsPipelineRef>::failure(
                RHIErrorCode::Unsupported,
                "Graphics pipeline vertex-buffer count exceeds the device limit.");
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
            const RHIFormatCapabilities format_support = format_capabilities(attribute.format);
            if (!EnumHasAnyFlags(format_support.usage, RHIFormatUsage::VertexBuffer))
            {
                return RHIResult<RHIGraphicsPipelineRef>::failure(
                    RHIErrorCode::Unsupported,
                    "Graphics pipeline vertex attribute format is not supported by the device.");
            }
        }

        return graphics_pipeline_cache->get_or_create(
            desc,
            [this](const RHIGraphicsPipelineDesc& canonical_desc)
            {
                return create_graphics_pipeline_impl(canonical_desc);
            });
    }

    RHIStatus RHIDevice::begin_pipeline_creation()
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex);
        if (shutting_down)
        {
            return RHIStatus::failure(
                RHIErrorCode::NotReady,
                "Graphics pipeline creation is unavailable during RHI device shutdown.");
        }
        ++active_pipeline_creations;
        return RHIStatus::success();
    }

    void RHIDevice::end_pipeline_creation()
    {
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex);
            if (active_pipeline_creations > 0)
            {
                --active_pipeline_creations;
            }
        }
        lifecycle_changed.notify_all();
    }
}
