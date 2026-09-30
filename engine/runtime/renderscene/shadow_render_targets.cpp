#include "renderscene/shadow_render_targets.h"

#include <utility>

#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    RHIStatus ShadowRenderTargets::ensure_views(RHIDevice& device, std::size_t view_count)
    {
        if (device_ && device_ != &device)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shadow targets belong to another device.");
        if (views_.size() == view_count * k_cascade_count && device_ == &device)
            return RHIStatus::success();
        release();
        RHISamplerDesc sampler_desc;
        sampler_desc.min_filter = RHIFilter::Nearest;
        sampler_desc.mag_filter = RHIFilter::Nearest;
        sampler_desc.mip_filter = RHIFilter::Nearest;
        sampler_desc.address_u = RHIAddressMode::ClampToEdge;
        sampler_desc.address_v = RHIAddressMode::ClampToEdge;
        sampler_desc.address_w = RHIAddressMode::ClampToEdge;
        sampler_desc.compare_enable = false;
        sampler_desc.debug_name = "ShadowRenderTargets.GatherSampler";
        auto sampler = device.create_sampler(sampler_desc);
        if (!sampler) return sampler.status();
        std::vector<ViewTarget> created;
        created.reserve(view_count * k_cascade_count);
        for (std::size_t index = 0; index < view_count * k_cascade_count; ++index)
        {
            RHITextureDesc desc;
            desc.width = k_resolution;
            desc.height = k_resolution;
            desc.format = PixelFormat::D32Float;
            desc.usage = RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource;
            desc.initial_access = RHIAccess::Common;
            desc.clear_value = RHIClearValue::DepthZero;
            desc.debug_name = "ShadowRenderTargets.Depth";
            auto texture = device.create_texture(desc);
            if (!texture) return texture.status();
            RHITextureViewDesc depth;
            depth.type = RHIResourceViewType::DepthStencil;
            depth.format = desc.format;
            depth.subresources.aspect = RHITextureAspect::Depth;
            depth.debug_name = "ShadowRenderTargets.DSV";
            auto depth_view = device.create_texture_view(texture.value(), depth);
            if (!depth_view) return depth_view.status();
            RHITextureViewDesc shader;
            shader.type = RHIResourceViewType::ShaderResource;
            shader.format = desc.format;
            shader.subresources.aspect = RHITextureAspect::Depth;
            shader.debug_name = "ShadowRenderTargets.SRV";
            auto shader_view = device.create_texture_view(texture.value(), shader);
            if (!shader_view) return shader_view.status();
            created.push_back({std::move(texture).value(), std::move(depth_view).value(),
                               std::move(shader_view).value(), RHIAccess::Common});
        }
        views_ = std::move(created);
        sampler_ = std::move(sampler).value();
        device_ = &device;
        return RHIStatus::success();
    }

    void ShadowRenderTargets::release() noexcept
    {
        views_.clear();
        sampler_.reset();
        device_ = nullptr;
    }

    void ShadowRenderTargets::publish_submitted_access() noexcept
    {
        for (ViewTarget& view : views_) view.access = RHIAccess::ShaderResourceGraphics;
    }
} // namespace toy3d
