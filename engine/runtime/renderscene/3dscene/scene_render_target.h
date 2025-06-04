#pragma once
#include "rhi/rhi_resource.h"
#include "core/math/math.h"


namespace toy3d
{
    //using namespace toy3d;
    struct RenderTargetDesc
    { 
        RenderTargetDesc()
        :format(EPixelFormat::Unknow)
        ,array_num(0)
        ,sample_num(0)
        ,flags(ETextureCreateFlags::Tex_None)
        {
            size.x = 0;
            size.y = 0;
            clear_value.x = clear_value.y = clear_value.z = clear_value.w = 0;
        }

        RenderTargetDesc(EPixelFormat tex_format
        , ETextureCreateFlags tex_flags
        , vec2 tex_size
        , std::string tex_name
        , uint32_t tex_msaa_num = 1
        , color default_value = color(0,0,0,0)
        , uint32_t tex_array_num = 1)
        {
            format = tex_format;
            array_num = tex_array_num;      // texture array num
            sample_num = tex_msaa_num;    // msaa
            flags = tex_flags;
            size = tex_size;
            clear_value = default_value;
            debug_name = tex_name;
        }

        bool operator==(const RenderTargetDesc & other)
        {
            return format == other.format
            && array_num == other.array_num
            && sample_num == other.sample_num
            && flags == other.flags
            && size == other.size
            && clear_value == other.clear_value
            && debug_name == other.debug_name;
        }

        bool operator<(const RenderTargetDesc& other) const {
            return (size == other.size && format < other.format)
				|| (size == other.size && format == other.format && array_num < other.array_num)
				|| (size == other.size && format == other.format && array_num == other.array_num && sample_num < other.sample_num)
				|| (size == other.size && format == other.format && array_num == other.array_num && sample_num == other.sample_num && flags < other.flags);
        }

        bool is_cube()
        {
            return array_num == 6;
        }

        EPixelFormat format;
        uint32_t array_num;      // texture array num
        uint32_t sample_num;    // msaa
        ETextureCreateFlags flags;
        vec2 size;
        color clear_value;
        std::string debug_name;
    };

    class SceneRenderTargetMgr
    {
    private:
        SceneRenderTargetMgr(){};
        ~SceneRenderTargetMgr(){};
        SceneRenderTargetMgr(const SceneRenderTargetMgr& other){}
        SceneRenderTargetMgr(const SceneRenderTargetMgr&& rhs){}
    public:
        static SceneRenderTargetMgr* Get()
        {
            static SceneRenderTargetMgr mgr;
            return &mgr;
        }
        void allocate();

        RHITexture* get_scene_color(){return scene_color.get();}

        RHITexture* get_scene_depth(){return scene_depth.get();}
    private:
        vec2 buffer_size;
        RHITextureRef scene_color;
        RHITextureRef scene_depth;

        std::map<RenderTargetDesc, RHITextureRef> render_target_pool;
    };
}