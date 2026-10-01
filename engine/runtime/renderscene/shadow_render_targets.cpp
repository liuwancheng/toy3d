#include "renderscene/shadow_render_targets.h"

#include <utility>

#include "drivers/rhi/rhi_device.h"

namespace toy3d
{
    RHIStatus build_shadow_atlas_layout(std::size_t cascade_count, std::uint32_t max_resolution,
                                       ShadowAtlasLayout& layout)
    {
        if (cascade_count < 1u || cascade_count > ShadowRenderTargets::k_max_cascade_count)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shadow cascade count is outside [1, 3].");
        if (max_resolution < LightSceneData::k_min_shadow_resolution ||
            max_resolution > LightSceneData::k_max_shadow_resolution ||
            (max_resolution & (max_resolution - 1u)) != 0u)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shadow maximum resolution must be 512, 1024 or 2048.");
        ShadowAtlasLayout candidate;
        candidate.cascade_count = cascade_count;
        candidate.max_resolution = max_resolution;
        candidate.width = cascade_count == 1u ? max_resolution : max_resolution + max_resolution / 2u;
        candidate.height = max_resolution;
        candidate.tiles[0] = {0u, 0u, max_resolution};
        for (std::size_t index = 1u; index < cascade_count; ++index)
            candidate.tiles[index] = {max_resolution, static_cast<std::uint32_t>(index - 1u) * (max_resolution / 2u),
                                      max_resolution / 2u};
        layout = candidate;
        return RHIStatus::success();
    }

    // --------------------------------------------------------------------------
    // ShadowRenderTargets: per-View depth atlas ownership and submitted access
    // --------------------------------------------------------------------------
    RHIStatus ShadowRenderTargets::ensure_views(RHIDevice& device, std::size_t view_count, std::size_t cascade_count, std::uint32_t requested_resolution)
    {
        ShadowAtlasLayout layout;
        RHIStatus status = build_shadow_atlas_layout(cascade_count, requested_resolution, layout);
        if (!status) return status;
        if (device_ && device_ != &device)
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "Shadow targets belong to another device.");
        const std::uint32_t limit = device.limits().max_texture_dimension_2d;
        while (layout.width > limit || layout.height > limit)
        {
            if (layout.max_resolution == LightSceneData::k_min_shadow_resolution)
                return RHIStatus::failure(RHIErrorCode::Unsupported, "Device cannot support the minimum shadow atlas size.");
            status = build_shadow_atlas_layout(cascade_count, layout.max_resolution / 2u, layout);
            if (!status) return status;
        }
        if (views_.size() == view_count && layout_.width == layout.width && layout_.height == layout.height && device_ == &device)
        {
            // Two and three cascades share the same atlas dimensions and tile positions.
            layout_ = layout;
            return RHIStatus::success();
        }
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
        created.reserve(view_count);
        for (std::size_t index = 0; index < view_count; ++index)
        {
            RHITextureDesc desc;
            desc.width = layout.width;
            desc.height = layout.height;
            desc.format = PixelFormat::D32Float;
            desc.usage = RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource;
            desc.initial_access = RHIAccess::Common;
            desc.clear_value = RHIClearValue::DepthZero;
            desc.debug_name = "ShadowRenderTargets.DepthAtlas";
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
        layout_ = layout;
        device_ = &device;
        return RHIStatus::success();
    }

    void ShadowRenderTargets::release() noexcept
    {
        views_.clear();
        sampler_.reset();
        device_ = nullptr;
        layout_ = {};
    }

    void ShadowRenderTargets::publish_submitted_access() noexcept
    {
        for (ViewTarget& view : views_) view.access = RHIAccess::ShaderResourceGraphics;
    }
} // namespace toy3d
