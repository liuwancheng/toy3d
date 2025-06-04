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
        RHIResourceCreateInfo color_create_info;
        color_create_info.access_type = ERHIAccess::EWritable;
        color_create_info.clear_value = ClearValueBinding(color_desc.clear_value);
        color_create_info.debug_name = "SceneColor";
        
        scene_color = g_rhi->create_texture2d(color_desc.size.x, color_desc.size.y, color_desc.format, 1, color_desc.sample_num, color_desc.flags, color_create_info);
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
        RHIResourceCreateInfo depth_create_info{};
        depth_create_info.access_type = ERHIAccess::EWritable;
        depth_create_info.clear_value = ClearValueBinding(1.0f, 0); // 默认深度1.0，模板0
        depth_create_info.debug_name = "SceneDepth";

        scene_depth = g_rhi->create_texture2d(desc_depth.size.x, desc_depth.size.y, desc_depth.format, 1, desc_depth.sample_num, desc_depth.flags, depth_create_info);
        render_target_pool[desc_depth] = scene_depth;
    }  
}

}//end namespace toy3d