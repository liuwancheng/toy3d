#include "scene_render_target.h"
#include "rhi/rhi.h"

namespace toy3d
{

void SceneRenderTargetMgr::allocate()
{
    // scene color
    RenderTargetDesc color_desc(EPixelFormat::R8G8B8A8, ETextureCreateFlags::Tex_RenderTarget, buffer_size, "SceneColor");

    auto iter = render_target_pool.find(color_desc);
    if(iter != render_target_pool.end())
    {
        scene_color = iter->second;
    }
    else
    {
        scene_color = g_rhi->create_texture2d(color_desc.size.x, color_desc.size.y, static_cast<uint8_t>(color_desc.format), color_desc.sample_num, static_cast<uint32_t>(color_desc.flags));
        render_target_pool[color_desc] = scene_color;
    }

    // scene depth&stencil
    RenderTargetDesc desc_depth(EPixelFormat::DepthStencil, ETextureCreateFlags::Tex_RenderTarget|ETextureCreateFlags::Tex_DepthStencilTarget, buffer_size, "SceneDepth");

    auto desc_iter = render_target_pool.find(desc_depth);
    if(desc_iter != render_target_pool.end())
    {
        scene_depth = desc_iter->second;
    }
    else
    {
        scene_depth = g_rhi->create_texture2d(desc_depth.size.x, desc_depth.size.y, static_cast<uint8_t>(desc_depth.format), desc_depth.sample_num, static_cast<uint32_t>(desc_depth.flags));
        render_target_pool[desc_depth] = scene_depth;
    }  
}

}//end namespace toy3d