#include "renderscene/scene_render_targets.h"

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_public_definitions.h"
#include "drivers/rhi/rhi_queue.h"

#include <utility>

namespace toy3d
{
    RHIStatus SceneRenderTargets::ensure_extent(
        RHIDevice& device,
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat scene_color_format)
    {
        if (width == 0u || height == 0u)
        {
            return RHIStatus::failure( RHIErrorCode::InvalidArgument, "SceneRenderTargets extent must be non-empty.");
        }
        if (scene_color_format == PixelFormat::Unknown)
        {
            return RHIStatus::failure(RHIErrorCode::InvalidArgument, "SceneRenderTargets requires a known SceneColor format.");
        }
        if (matches(width, height, scene_color_format))
        {
            return RHIStatus::success();
        }

        if (scene_color_texture_ || scene_depth_texture_)
        {
            const RHIStatus idle = device.graphics_queue().wait_idle();
            if (!idle)
            {
                return idle;
            }
        }
        release();
        return create_targets(device, width, height, scene_color_format);
    }

    void SceneRenderTargets::release() noexcept
    {
        scene_depth_shader_resource_view_.reset();
        scene_depth_view_.reset();
        scene_depth_texture_.reset();
        scene_color_shader_resource_view_.reset();
        scene_color_view_.reset();
        scene_color_texture_.reset();
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

    void SceneRenderTargets::publish_submitted_access(RHIAccess scene_color_access, RHIAccess scene_depth_access) noexcept
    {
        scene_color_access_ = scene_color_access;
        scene_depth_access_ = scene_depth_access;
    }

    bool SceneRenderTargets::matches(std::uint32_t width, std::uint32_t height, PixelFormat scene_color_format) const noexcept
    {
        return scene_color_texture_ && scene_color_view_ &&
            scene_color_shader_resource_view_ && scene_depth_texture_ &&
            scene_depth_view_ && scene_depth_shader_resource_view_ &&
            scene_color_texture_->desc().width == width &&
            scene_color_texture_->desc().height == height &&
            scene_color_texture_->desc().format == scene_color_format &&
            scene_depth_texture_->desc().width == width &&
            scene_depth_texture_->desc().height == height;
    }

    RHIStatus SceneRenderTargets::create_targets(
        RHIDevice& device,
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat scene_color_format)
    {
        RHITextureDesc color_desc;
        color_desc.width = width;
        color_desc.height = height;
        color_desc.format = scene_color_format;
        color_desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource;
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
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "SceneRenderTargets created no SceneColor texture.");
        }

        RHITextureViewDesc color_view_desc;
        color_view_desc.type = RHIResourceViewType::RenderTarget;
        color_view_desc.format = color_desc.format;
        color_view_desc.subresources.mip_count = 1u;
        color_view_desc.subresources.layer_count = 1u;
        color_view_desc.debug_name = "SceneRenderTargets.SceneColorRTV";
        RHIResult<RHITextureViewRef> color_view_result = device.create_texture_view(scene_color_texture_, color_view_desc);
        if (!color_view_result)
        {
            release();
            return color_view_result.status();
        }
        scene_color_view_ = std::move(color_view_result).value();
        if (!scene_color_view_)
        {
            release();
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "SceneRenderTargets created no SceneColor render-target view.");
        }

        RHITextureViewDesc color_srv_desc;
        color_srv_desc.type = RHIResourceViewType::ShaderResource;
        color_srv_desc.format = color_desc.format;
        color_srv_desc.subresources.mip_count = 1u;
        color_srv_desc.subresources.layer_count = 1u;
        color_srv_desc.debug_name = "SceneRenderTargets.SceneColorSRV";
        RHIResult<RHITextureViewRef> color_srv_result = device.create_texture_view(scene_color_texture_, color_srv_desc);
        if (!color_srv_result)
        {
            release();
            return color_srv_result.status();
        }
        scene_color_shader_resource_view_ = std::move(color_srv_result).value();
        if (!scene_color_shader_resource_view_)
        {
            release();
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "SceneRenderTargets created no SceneColor shader-resource view.");
        }

        RHITextureDesc depth_desc;
        depth_desc.width = width;
        depth_desc.height = height;
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
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "SceneRenderTargets created no SceneDepth texture.");
        }

        RHITextureViewDesc depth_view_desc;
        depth_view_desc.type = RHIResourceViewType::DepthStencil;
        depth_view_desc.format = depth_desc.format;
        depth_view_desc.subresources.aspect = RHITextureAspect::Depth;
        depth_view_desc.subresources.mip_count = 1u;
        depth_view_desc.subresources.layer_count = 1u;
        depth_view_desc.debug_name = "SceneRenderTargets.SceneDepthDSV";
        RHIResult<RHITextureViewRef> depth_view_result = device.create_texture_view(scene_depth_texture_, depth_view_desc);
        if (!depth_view_result)
        {
            release();
            return depth_view_result.status();
        }
        scene_depth_view_ = std::move(depth_view_result).value();
        if (!scene_depth_view_)
        {
            release();
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "SceneRenderTargets created no SceneDepth view.");
        }

        RHITextureViewDesc depth_srv_desc;
        depth_srv_desc.type = RHIResourceViewType::ShaderResource;
        depth_srv_desc.format = depth_desc.format;
        depth_srv_desc.subresources.aspect = RHITextureAspect::Depth;
        depth_srv_desc.subresources.mip_count = 1u;
        depth_srv_desc.subresources.layer_count = 1u;
        depth_srv_desc.debug_name = "SceneRenderTargets.SceneDepthSRV";
        RHIResult<RHITextureViewRef> depth_srv_result = device.create_texture_view(scene_depth_texture_, depth_srv_desc);
        if (!depth_srv_result)
        {
            release();
            return depth_srv_result.status();
        }
        scene_depth_shader_resource_view_ = std::move(depth_srv_result).value();
        if (!scene_depth_shader_resource_view_)
        {
            release();
            return RHIStatus::failure(
                RHIErrorCode::BackendFailure,
                "SceneRenderTargets created no SceneDepth shader-resource view.");
        }

        scene_color_access_ = RHIAccess::Common;
        scene_depth_access_ = RHIAccess::Common;
        return RHIStatus::success();
    }
}
