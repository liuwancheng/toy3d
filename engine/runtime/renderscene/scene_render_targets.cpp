#include "renderscene/scene_render_targets.h"

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_public_definitions.h"

#include <utility>

namespace toy3d
{
    RHIStatus SceneRenderTargets::ensure_extent(RHIDevice& device, const Extent& extent)
    {
        if (extent.width == 0u || extent.height == 0u)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "SceneRenderTargets extent must be non-empty.");
        }
        if (owning_device_ != nullptr && owning_device_ != &device)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument,
                                      "SceneRenderTargets cannot be reused or retired through a different RHI device.");
        }
        if (matches(extent))
        {
            return RHIStatus::success();
        }

        // Submitted command lists and backend deferred deletion keep old images
        // alive until their GPU use completes, so panel resizing needs no queue stall.
        release();
        return create_targets(device, extent);
    }

    void SceneRenderTargets::release() noexcept
    {
        shadow_targets_.release();
        scene_depth_shader_resource_view_.reset();
        scene_depth_view_.reset();
        scene_depth_texture_.reset();
        scene_color_shader_resource_view_.reset();
        scene_color_view_.reset();
        scene_color_texture_.reset();
        owning_device_ = nullptr;
        scene_color_access_ = RHIAccess::Common;
        scene_depth_access_ = RHIAccess::Common;
    }

    const RHITextureRef& SceneRenderTargets::scene_color_texture() const noexcept
    {
        return scene_color_texture_;
    }

    const RHITextureViewRef& SceneRenderTargets::scene_color_view() const noexcept
    {
        return scene_color_view_;
    }

    const RHITextureViewRef& SceneRenderTargets::scene_color_shader_resource_view() const noexcept
    {
        return scene_color_shader_resource_view_;
    }

    const RHITextureRef& SceneRenderTargets::scene_depth_texture() const noexcept
    {
        return scene_depth_texture_;
    }

    const RHITextureViewRef& SceneRenderTargets::scene_depth_view() const noexcept
    {
        return scene_depth_view_;
    }

    const RHITextureViewRef& SceneRenderTargets::scene_depth_shader_resource_view() const noexcept
    {
        return scene_depth_shader_resource_view_;
    }

    RHIAccess SceneRenderTargets::scene_color_access() const noexcept
    {
        return scene_color_access_;
    }

    RHIAccess SceneRenderTargets::scene_depth_access() const noexcept
    {
        return scene_depth_access_;
    }

    void SceneRenderTargets::publish_submitted_access(RHIAccess scene_color_access,
                                                      RHIAccess scene_depth_access) noexcept
    {
        scene_color_access_ = scene_color_access;
        scene_depth_access_ = scene_depth_access;
        shadow_targets_.publish_submitted_access();
    }

    bool SceneRenderTargets::matches(const Extent& extent) const noexcept
    {
        return scene_color_texture_ && scene_color_view_ && scene_color_shader_resource_view_ && scene_depth_texture_ &&
               scene_depth_view_ && scene_depth_shader_resource_view_ &&
               scene_color_texture_->desc().width == extent.width &&
               scene_color_texture_->desc().height == extent.height &&
               scene_color_texture_->desc().format == PixelFormat::R16G16B16A16Float &&
               scene_depth_texture_->desc().width == extent.width &&
               scene_depth_texture_->desc().height == extent.height;
    }

    RHIStatus SceneRenderTargets::create_targets(RHIDevice& device, const Extent& extent)
    {
        RHITextureDesc color_desc;
        color_desc.width = extent.width;
        color_desc.height = extent.height;
        color_desc.format = PixelFormat::R16G16B16A16Float;
        color_desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
        color_desc.initial_access = RHIAccess::Common;
        color_desc.clear_value = RHIClearValue::color_value(vec4(0.0F, 0.0F, 0.0F, 1.0F));
        color_desc.debug_name = "SceneRenderTargets.SceneColor";
        RHIResult<RHITextureRef> color_result = device.create_texture(color_desc);
        if (!color_result)
        {
            return color_result.status();
        }
        scene_color_texture_ = std::move(color_result).value();
        if (!scene_color_texture_)
        {
            release();
            return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                      "SceneRenderTargets created no SceneColor texture.");
        }

        RHITextureViewDesc color_view_desc;
        color_view_desc.type = RHIResourceViewType::RenderTarget;
        color_view_desc.format = color_desc.format;
        color_view_desc.subresources.mip_count = 1u;
        color_view_desc.subresources.layer_count = 1u;
        color_view_desc.debug_name = "SceneRenderTargets.SceneColorRTV";
        RHIResult<RHITextureViewRef> color_view_result =
            device.create_texture_view(scene_color_texture_, color_view_desc);
        if (!color_view_result)
        {
            release();
            return color_view_result.status();
        }
        scene_color_view_ = std::move(color_view_result).value();
        if (!scene_color_view_)
        {
            release();
            return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                      "SceneRenderTargets created no SceneColor render-target view.");
        }

        RHITextureViewDesc color_srv_desc;
        color_srv_desc.type = RHIResourceViewType::ShaderResource;
        color_srv_desc.format = color_desc.format;
        color_srv_desc.subresources.mip_count = 1u;
        color_srv_desc.subresources.layer_count = 1u;
        color_srv_desc.debug_name = "SceneRenderTargets.SceneColorSRV";
        RHIResult<RHITextureViewRef> color_srv_result =
            device.create_texture_view(scene_color_texture_, color_srv_desc);
        if (!color_srv_result)
        {
            release();
            return color_srv_result.status();
        }
        scene_color_shader_resource_view_ = std::move(color_srv_result).value();
        if (!scene_color_shader_resource_view_)
        {
            release();
            return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                      "SceneRenderTargets created no SceneColor shader-resource view.");
        }

        RHITextureDesc depth_desc;
        depth_desc.width = extent.width;
        depth_desc.height = extent.height;
        depth_desc.format = PixelFormat::D32Float;
        depth_desc.usage = RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource;
        depth_desc.initial_access = RHIAccess::Common;
        depth_desc.clear_value = RHIClearValue::DepthZero;
        depth_desc.debug_name = "SceneRenderTargets.SceneDepth";
        RHIResult<RHITextureRef> depth_result = device.create_texture(depth_desc);
        if (!depth_result)
        {
            release();
            return depth_result.status();
        }
        scene_depth_texture_ = std::move(depth_result).value();
        if (!scene_depth_texture_)
        {
            release();
            return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                      "SceneRenderTargets created no SceneDepth texture.");
        }

        RHITextureViewDesc depth_view_desc;
        depth_view_desc.type = RHIResourceViewType::DepthStencil;
        depth_view_desc.format = depth_desc.format;
        depth_view_desc.subresources.aspect = RHITextureAspect::Depth;
        depth_view_desc.subresources.mip_count = 1u;
        depth_view_desc.subresources.layer_count = 1u;
        depth_view_desc.debug_name = "SceneRenderTargets.SceneDepthDSV";
        RHIResult<RHITextureViewRef> depth_view_result =
            device.create_texture_view(scene_depth_texture_, depth_view_desc);
        if (!depth_view_result)
        {
            release();
            return depth_view_result.status();
        }
        scene_depth_view_ = std::move(depth_view_result).value();
        if (!scene_depth_view_)
        {
            release();
            return RHIStatus::failure(RHIErrorCode::BackendFailure, "SceneRenderTargets created no SceneDepth view.");
        }

        RHITextureViewDesc depth_srv_desc;
        depth_srv_desc.type = RHIResourceViewType::ShaderResource;
        depth_srv_desc.format = depth_desc.format;
        depth_srv_desc.subresources.aspect = RHITextureAspect::Depth;
        depth_srv_desc.subresources.mip_count = 1u;
        depth_srv_desc.subresources.layer_count = 1u;
        depth_srv_desc.debug_name = "SceneRenderTargets.SceneDepthSRV";
        RHIResult<RHITextureViewRef> depth_srv_result =
            device.create_texture_view(scene_depth_texture_, depth_srv_desc);
        if (!depth_srv_result)
        {
            release();
            return depth_srv_result.status();
        }
        scene_depth_shader_resource_view_ = std::move(depth_srv_result).value();
        if (!scene_depth_shader_resource_view_)
        {
            release();
            return RHIStatus::failure(RHIErrorCode::BackendFailure,
                                      "SceneRenderTargets created no SceneDepth shader-resource view.");
        }

        scene_color_access_ = RHIAccess::Common;
        scene_depth_access_ = RHIAccess::Common;
        owning_device_ = &device;
        return RHIStatus::success();
    }
} // namespace toy3d
