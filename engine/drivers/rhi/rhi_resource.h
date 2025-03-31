#pragma once
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <bitset>

#include <vector>
#include <stdlib.h>
#include <stdint.h>
#include <iostream>
#include <atomic>

#include "math/math.h"

#define MAX_GBUFFER_NUM 4

namespace toy3d
{
    enum class ERenderTargetLoadAction : uint8_t
    {
        ENoAction,
        ELoad,
        EClear,
        EDontCare
    };

    enum class ERenderTargetStoreAction : uint8_t
    {
        ENoAction,
        EStore,
        EDontCare,
        EMultisampleResolve
    };

    enum class ESubpassHint : uint8_t
    {
        None,
        DepthReadSubpass,       // subpass need read depth
        ColorReadSubpass,       // subpass need read scene color
        DeferredShadingSubpass  // mobile defferred shading subpass
    };

    enum class EPixelFormat : uint8_t
    {
        Unknow,
        A32B32G32R32F,
        B8G8R8A8,
        G8,
        G16,
        DXT1,
        DXT3,
        DXT5,
        UYVY,
        FloatRGB,
        FloatRGBA,
        DepthStencil,
        ShadowDepth,
        R32_Float,
        G16R16,
        G16R16F,
        G32R32F,
        A2B10G10R10,
        A16G16B16R16,
        R16G16B16A16,
        Depth24,
        FloatR11G11B10,
        A8,
        R32_UINT,
        PVRTC2,
        PVRTC4,
        R8G8B8A8,
        A8R8G8B8,
        ASTC_4x4,
        ASTC_6x6,
        ASTC_8x8,
        ASTC_12x12,
        R8G8B8A8_SNORM,
        HDR,
        MAX
    };

    enum ETextureCreateFlags : uint32_t
    {
        Tex_None = 0,               // normal texture
        Tex_RenderTarget            = 1<<0,
        Tex_ResolveTarget           = 1<<1,
        Tex_DepthStencilTarget      = 1<<2,
        Tex_ShaderResource          = 1<<3,     // can be used as a shader resource
        Tex_SRGB                    = 1<<4,     // gamma space
        Tex_Dynamic                 = 1<<5,     // 动态贴图，可能每帧都更新
        Tex_Memoryless              = 1<<6,
        Tex_Virtual                 = 1<<7,
        Tex_Transient               = 1<<8      // 临时申请的资源
    };

    /* 所有RHI资源基类，本来想模仿UE用引用技术。改为用c++智能指针**/
    class RHIResoruce
    {
    public:
        RHIResoruce()
        {
        }
        virtual ~RHIResoruce()
        {
        }
    };

    class RHITexture : public RHIResoruce
    {
    public:
        RHITexture(EPixelFormat _format,ETextureCreateFlags _flags,uint32_t _mips, uint32_t _samples)
        :format(_format)
        ,flags(_flags)
        ,mips_num(_mips)
        ,samples_num(_samples){}

        virtual class RHITexture2D* cast_texture2d(){return nullptr;}
        virtual class RHITextureCube* cast_texture_cube(){return nullptr;}
        virtual class RHITexture3D* cast_texture3d(){return nullptr;}

        virtual vec2 get_size() const = 0;
    public:
        bool is_msaa(){return samples_num > 0;}

        EPixelFormat get_format(){return format;}

        ETextureCreateFlags get_flags(){return flags;}

        uint32_t get_samples_num(){return samples_num;}

        uint32_t get_mips_num(){return mips_num;}

        void set_texture_name(std::string name){tex_name = name;}
    private:
        EPixelFormat format;
        ETextureCreateFlags flags;
        uint32_t mips_num;
        uint32_t samples_num;
        std::string tex_name;
    };

    class RHITexture2D : public RHITexture
    {
    public:
        RHITexture2D(uint32_t w, uint32_t h
            ,EPixelFormat _format,ETextureCreateFlags _flags,uint32_t _mips, uint32_t _samples)
        :RHITexture(_format, _flags, _mips,_samples)
        ,size_x(w)
        ,size_y(h){}

        virtual RHITexture2D* cast_texture2d(){return this;}
        virtual vec2 get_size(){return vec2(size_x, size_y);}
    private:
        uint32_t size_x;
        uint32_t size_y;
    };

    // class RHITextureCube : public RHITexture
    // {};

    // class RHITexture3D : public RHITexture
    // {};

    struct RHIRenderPassInfo
    {
        struct ColorEntry
        {
            RHITexture* render_target;
            RHITexture* resolve_target;
            ERenderTargetLoadAction load_action;
            ERenderTargetStoreAction store_action;
        };
        ColorEntry color_render_targets[MAX_GBUFFER_NUM];

        struct DepthStencilEntry
        {
            RHITexture* depth_stencil_target;
            RHITexture* resolve_target;
            ERenderTargetLoadAction load_action;
            ERenderTargetStoreAction store_action;
        };
        DepthStencilEntry depth_stencil_render_target;
        ESubpassHint subpass = ESubpassHint::None;

        // color , no depth, other optional
        explicit RHIRenderPassInfo(RHITexture* color_rt, 
                ERenderTargetLoadAction load_action,
                ERenderTargetStoreAction store_action,
                RHITexture* resolve_rt = nullptr)
        {
            color_render_targets[0].render_target = color_rt;
            color_render_targets[0].resolve_target = resolve_rt;
            color_render_targets[0].load_action = load_action;
            color_render_targets[0].store_action = store_action;
            depth_stencil_render_target.depth_stencil_target = nullptr;
            depth_stencil_render_target.resolve_target = nullptr;
            depth_stencil_render_target.load_action = ERenderTargetLoadAction::ENoAction;
            depth_stencil_render_target.store_action = ERenderTargetStoreAction::ENoAction;

            memset(&color_render_targets[1], 0, sizeof(ColorEntry)*(MAX_GBUFFER_NUM-1));
        }

        // color , depth, other optional
        explicit RHIRenderPassInfo(RHITexture* color_rt, 
                ERenderTargetLoadAction load_action,
                ERenderTargetStoreAction store_action,
                RHITexture* depth_stencil_rt,
                ERenderTargetLoadAction dp_load_action,
                ERenderTargetStoreAction dp_store_action,
                RHITexture* resolve_rt = nullptr,
                RHITexture* dp_resolve_rt = nullptr)
        {
            color_render_targets[0].render_target = color_rt;
            color_render_targets[0].resolve_target = resolve_rt;
            color_render_targets[0].load_action = load_action;
            color_render_targets[0].store_action = store_action;
            depth_stencil_render_target.depth_stencil_target = depth_stencil_rt;
            depth_stencil_render_target.resolve_target = dp_resolve_rt;
            depth_stencil_render_target.load_action = dp_load_action;
            depth_stencil_render_target.store_action = dp_store_action;

            memset(&color_render_targets[1], 0, sizeof(ColorEntry)*(MAX_GBUFFER_NUM-1));
        }

        uint8_t get_color_rt_num() const
        {
            uint8_t num = 0;
            for(const ColorEntry& entry : color_render_targets)
            {
                if(!entry.render_target)
                    return num;
                num++;
            }
            return num;
        }

        bool enable_depth_test() const {return !!depth_stencil_render_target.depth_stencil_target;}
    };

    using RHITextureRef = std::shared_ptr<RHITexture>;
    
}