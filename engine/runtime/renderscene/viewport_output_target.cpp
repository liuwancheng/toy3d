#include "renderscene/viewport_output_target.h"

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_descriptors.h"

#include <utility>

namespace toy3d
{
    RHIStatus ViewportOutputTarget::ensure_extent(RHIDevice& device, const Extent& extent)
    {
        if (extent.width == 0u || extent.height == 0u)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Viewport output extent must be non-empty.");
        }
        if (owning_device_ != nullptr && owning_device_ != &device)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Viewport output belongs to another device.");
        }
        if (texture_ && render_target_view_ && shader_resource_view_ && texture_->desc().width == extent.width &&
            texture_->desc().height == extent.height)
        {
            return RHIStatus::success();
        }

        RHITextureDesc desc;
        desc.width = extent.width;
        desc.height = extent.height;
        desc.format = PixelFormat::B8G8R8A8UNorm;
        desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
        desc.initial_access = RHIAccess::Common;
        desc.debug_name = "SceneViewport.SDR";
        RHIResult<RHITextureRef> created = device.create_texture(desc);
        if (!created)
        {
            return created.status();
        }
        RHITextureRef texture = std::move(created).value();

        RHITextureViewDesc render_view_desc;
        render_view_desc.type = RHIResourceViewType::RenderTarget;
        render_view_desc.format = desc.format;
        render_view_desc.debug_name = "SceneViewport.SDR.RTV";
        RHIResult<RHITextureViewRef> created_render_view = device.create_texture_view(texture, render_view_desc);
        if (!created_render_view)
        {
            return created_render_view.status();
        }

        RHITextureViewDesc shader_view_desc;
        shader_view_desc.type = RHIResourceViewType::ShaderResource;
        shader_view_desc.format = desc.format;
        shader_view_desc.debug_name = "SceneViewport.SDR.SRV";
        RHIResult<RHITextureViewRef> created_shader_view = device.create_texture_view(texture, shader_view_desc);
        if (!created_shader_view)
        {
            return created_shader_view.status();
        }

        // The submitted command list and backend deferred deletion retain old GPU use.
        shader_resource_view_ = std::move(created_shader_view).value();
        render_target_view_ = std::move(created_render_view).value();
        texture_ = std::move(texture);
        owning_device_ = &device;
        access_ = RHIAccess::Common;
        return RHIStatus::success();
    }

    void ViewportOutputTarget::release() noexcept
    {
        shader_resource_view_.reset();
        render_target_view_.reset();
        texture_.reset();
        owning_device_ = nullptr;
        access_ = RHIAccess::Common;
    }
} // namespace toy3d
