#include "renderscene/3dscene/scene_render_target.h"

#include <tuple>

namespace toy3d
{
    bool RenderTargetDesc::operator<(const RenderTargetDesc& other) const
    {
        return std::tie(format, width, height, array_layers, sample_count, usage, debug_name) <
            std::tie(other.format, other.width, other.height, other.array_layers,
                other.sample_count, other.usage, other.debug_name);
    }

    SceneRenderTargetMgr& SceneRenderTargetMgr::get()
    {
        static SceneRenderTargetMgr manager;
        return manager;
    }

    void SceneRenderTargetMgr::set_buffer_size(std::uint32_t width, std::uint32_t height)
    {
        buffer_width = width;
        buffer_height = height;
    }

    RHIStatus SceneRenderTargetMgr::allocate(RHIDevice& device)
    {
        RenderTargetDesc color_desc;
        color_desc.format = RHIFormat::R8G8B8A8UNorm;
        color_desc.width = buffer_width;
        color_desc.height = buffer_height;
        color_desc.usage = RHIResourceUsage::RenderTarget;
        color_desc.clear_value = RHIClearValue::Black;
        color_desc.debug_name = "SceneColor";

        RHIResult<RHITextureRef> color_result = find_or_create(device, color_desc);
        if (!color_result)
        {
            return color_result.status();
        }

        RenderTargetDesc depth_desc;
        depth_desc.format = RHIFormat::DepthStencil;
        depth_desc.width = buffer_width;
        depth_desc.height = buffer_height;
        depth_desc.usage = RHIResourceUsage::DepthStencil;
        depth_desc.clear_value = RHIClearValue::DepthOne;
        depth_desc.debug_name = "SceneDepth";

        RHIResult<RHITextureRef> depth_result = find_or_create(device, depth_desc);
        if (!depth_result)
        {
            return depth_result.status();
        }

        scene_color_texture = color_result.value();
        scene_depth_texture = depth_result.value();
        return RHIStatus::success();
    }

    const RHITextureRef& SceneRenderTargetMgr::scene_color() const
    {
        return scene_color_texture;
    }

    const RHITextureRef& SceneRenderTargetMgr::scene_depth() const
    {
        return scene_depth_texture;
    }

    RHIResult<RHITextureRef> SceneRenderTargetMgr::find_or_create(
        RHIDevice& device,
        const RenderTargetDesc& desc)
    {
        const auto existing = render_target_pool.find(desc);
        if (existing != render_target_pool.end())
        {
            return RHIResult<RHITextureRef>::success(existing->second);
        }

        RHITextureDesc texture_desc;
        texture_desc.width = desc.width;
        texture_desc.height = desc.height;
        texture_desc.array_layers = desc.array_layers;
        texture_desc.sample_count = desc.sample_count;
        texture_desc.format = desc.format;
        texture_desc.usage = desc.usage;
        texture_desc.initial_access = RHIAccess::Unknown;
        texture_desc.clear_value = desc.clear_value;
        texture_desc.debug_name = desc.debug_name;

        RHIResult<RHITextureRef> result = device.create_texture(texture_desc);
        if (result)
        {
            render_target_pool.emplace(desc, result.value());
        }
        return result;
    }
}
