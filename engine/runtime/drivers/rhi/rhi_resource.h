#pragma once
#include "core/misc/pch.h"
#include "core/math/math.h"
#include "rhi_definitions.h"


namespace toy3d
{
    /* 所有RHI资源基类，本来想模仿UE用引用技术。改为用c++智能指针**/
    class RHIResource
    {
    public:
        RHIResource()
        {
        }
        virtual ~RHIResource()
        {
        }
    };

    class RHIRasterizerState : public RHIResource
    {
    public:
        virtual bool GetInitializer(struct RasterizerStateInitializerRHI& Init) { return false; }
    };

    class RHIDepthStencilState : public RHIResource
    {
    public:
        virtual bool GetInitializer(struct DepthStencilStateInitializerRHI& Init) { return false; }
    };

    class RHIBlendState : public RHIResource
    {
    public:
        virtual bool GetInitializer(class BlendStateInitializerRHI& Init) { return false; }
    };

    class RHISamplerState : public RHIResource 
    {
    public:
        virtual bool IsImmutable() const { return false; }
    };

    // Texture base class
    class RHITexture : public RHIResource
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
        RHITexture2D(uint32_t w, uint32_t h, EPixelFormat _format, ETextureCreateFlags _flags, uint32_t _mips, uint32_t _samples)
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
        ColorEntry color_render_targets[MaxSimultaneousRenderTargets];

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

            memset(&color_render_targets[1], 0, sizeof(ColorEntry)*(MaxSimultaneousRenderTargets-1));
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

            memset(&color_render_targets[1], 0, sizeof(ColorEntry)*(MaxSimultaneousRenderTargets-1));
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

    class FExclusiveDepthStencil
    {
    public:
        enum Type
        {
            // don't use those directly, use the combined versions below
            // 4 bits are used for depth and 4 for stencil to make the hex value readable and non overlapping
            DepthNop = 0x00,
            DepthRead = 0x01,
            DepthWrite = 0x02,
            DepthMask = 0x0f,
            StencilNop = 0x00,
            StencilRead = 0x10,
            StencilWrite = 0x20,
            StencilMask = 0xf0,

            // use those:
            DepthNop_StencilNop = DepthNop + StencilNop,
            DepthRead_StencilNop = DepthRead + StencilNop,
            DepthWrite_StencilNop = DepthWrite + StencilNop,
            DepthNop_StencilRead = DepthNop + StencilRead,
            DepthRead_StencilRead = DepthRead + StencilRead,
            DepthWrite_StencilRead = DepthWrite + StencilRead,
            DepthNop_StencilWrite = DepthNop + StencilWrite,
            DepthRead_StencilWrite = DepthRead + StencilWrite,
            DepthWrite_StencilWrite = DepthWrite + StencilWrite,
        };

    private:
        Type Value;

    public:
        // constructor
        FExclusiveDepthStencil(Type InValue = DepthNop_StencilNop)
            : Value(InValue)
        {
        }

        inline bool IsUsingDepthStencil() const
        {
            return Value != DepthNop_StencilNop;
        }
        inline bool IsUsingDepth() const
        {
            return (ExtractDepth() != DepthNop);
        }
        inline bool IsUsingStencil() const
        {
            return (ExtractStencil() != StencilNop);
        }
        inline bool IsDepthWrite() const
        {
            return ExtractDepth() == DepthWrite;
        }
        inline bool IsDepthRead() const
        {
            return ExtractDepth() == DepthRead;
        }
        inline bool IsStencilWrite() const
        {
            return ExtractStencil() == StencilWrite;
        }
        inline bool IsStencilRead() const
        {
            return ExtractStencil() == StencilRead;
        }

        inline bool IsAnyWrite() const
        {
            return IsDepthWrite() || IsStencilWrite();
        }

        inline void SetDepthWrite()
        {
            Value = (Type)(ExtractStencil() | DepthWrite);
        }
        inline void SetStencilWrite()
        {
            Value = (Type)(ExtractDepth() | StencilWrite);
        }
        inline void SetDepthStencilWrite(bool bDepth, bool bStencil)
        {
            Value = DepthNop_StencilNop;

            if (bDepth)
            {
                SetDepthWrite();
            }
            if (bStencil)
            {
                SetStencilWrite();
            }
        }
        bool operator==(const FExclusiveDepthStencil& rhs) const
        {
            return Value == rhs.Value;
        }

        bool operator != (const FExclusiveDepthStencil& RHS) const
        {
            return Value != RHS.Value;
        }

        inline bool IsValid(FExclusiveDepthStencil& Current) const
        {
            Type Depth = ExtractDepth();

            if (Depth != DepthNop && Depth != Current.ExtractDepth())
            {
                return false;
            }

            Type Stencil = ExtractStencil();

            if (Stencil != StencilNop && Stencil != Current.ExtractStencil())
            {
                return false;
            }

            return true;
        }

        inline void GetAccess(ERHIAccess& DepthAccess, ERHIAccess& StencilAccess) const
        {
            DepthAccess = ERHIAccess::None;

            // SRV access is allowed whilst a depth stencil target is "readable".
            ERHIAccess DSVReadOnlyMask = static_cast<ERHIAccess>(
                static_cast<uint32>(ERHIAccess::DSVRead) | 
                static_cast<uint32>(ERHIAccess::SRVGraphics) | 
                static_cast<uint32>(ERHIAccess::SRVCompute)
            );

            // If write access is required, only the depth block can access the resource.
            ERHIAccess DSVReadWriteMask = static_cast<ERHIAccess>(
                static_cast<uint32>(ERHIAccess::DSVRead) | 
                static_cast<uint32>(ERHIAccess::DSVWrite)
            );

            if (IsUsingDepth())
            {
                DepthAccess = IsDepthWrite() ? DSVReadWriteMask : DSVReadOnlyMask;
            }

            StencilAccess = ERHIAccess::None;

            if (IsUsingStencil())
            {
                StencilAccess = IsStencilWrite() ? DSVReadWriteMask : DSVReadOnlyMask;
            }
        }

        template <typename TFunction>
        inline void EnumerateSubresources(TFunction Function) const
        {
            if (!IsUsingDepthStencil())
            {
                return;
            }

            ERHIAccess DepthAccess = ERHIAccess::None;
            ERHIAccess StencilAccess = ERHIAccess::None;
            GetAccess(DepthAccess, StencilAccess);

            // Same depth / stencil state; single subresource.
            if (DepthAccess == StencilAccess)
            {
                Function(DepthAccess, FRHITransitionInfo::kAllSubresources);
            }
            // Separate subresources for depth / stencil.
            else
            {
                if (DepthAccess != ERHIAccess::None)
                {
                    Function(DepthAccess, FRHITransitionInfo::kDepthPlaneSlice);
                }
                if (StencilAccess != ERHIAccess::None)
                {
                    Function(StencilAccess, FRHITransitionInfo::kStencilPlaneSlice);
                }
            }
        }

        inline FExclusiveDepthStencil GetReadableTransition() const
        {
            FExclusiveDepthStencil::Type NewDepthState = IsDepthWrite()
                ? FExclusiveDepthStencil::DepthRead
                : FExclusiveDepthStencil::DepthNop;

            FExclusiveDepthStencil::Type NewStencilState = IsStencilWrite()
                ? FExclusiveDepthStencil::StencilRead
                : FExclusiveDepthStencil::StencilNop;

            return (FExclusiveDepthStencil::Type)(NewDepthState | NewStencilState);
        }

        inline FExclusiveDepthStencil GetWritableTransition() const
        {
            FExclusiveDepthStencil::Type NewDepthState = IsDepthRead()
                ? FExclusiveDepthStencil::DepthWrite
                : FExclusiveDepthStencil::DepthNop;

            FExclusiveDepthStencil::Type NewStencilState = IsStencilRead()
                ? FExclusiveDepthStencil::StencilWrite
                : FExclusiveDepthStencil::StencilNop;

            return (FExclusiveDepthStencil::Type)(NewDepthState | NewStencilState);
        }

        uint32 GetIndex() const
        {
            switch (Value)
            {
            case DepthWrite_StencilNop:
            case DepthNop_StencilWrite:
            case DepthWrite_StencilWrite:
            case DepthNop_StencilNop:
                return 0; // old DSAT_Writable

            case DepthRead_StencilNop:
            case DepthRead_StencilWrite:
                return 1; // old DSAT_ReadOnlyDepth

            case DepthNop_StencilRead:
            case DepthWrite_StencilRead:
                return 2; // old DSAT_ReadOnlyStencil

            case DepthRead_StencilRead:
                return 3; // old DSAT_ReadOnlyDepthAndStencil
            }
            return -1;
        }
        static const uint32 MaxIndex = 4;

    private:
        inline Type ExtractDepth() const
        {
            return (Type)(Value & DepthMask);
        }
        inline Type ExtractStencil() const
        {
            return (Type)(Value & StencilMask);
        }
    };

    //
    // Shader bindings
    //

    typedef std::array<struct VertexElement, MaxVertexElementCount> VertexDeclarationElementList;
    class RHIVertexDeclaration : public RHIResource
    {
    public:
        virtual bool GetInitializer(VertexDeclarationElementList& Init) { return false; }
    };

    class RHIBoundShaderState : public RHIResource {};

    //
    // Shaders
    //

    class RHIShader : public RHIResource
    {
    public:
        void SetHash(std::size_t InHash) { Hash = InHash; }
        std::size_t GetHash() const { return Hash; }

        explicit RHIShader(EShaderFrequency InFrequency)
            : Frequency(InFrequency)
        {
        }

        inline EShaderFrequency GetFrequency() const
        {
            return Frequency;
        }

    #if (BUILD_DEBUG || BUILD_DEVELOPMENT)
        std::string ShaderName;
        const std::string GetShaderName() const { return ShaderName; }
    #else
        const std::string GetShaderName() const { return ""; }
    #endif
    private:
        std::size_t Hash;
        EShaderFrequency Frequency;
    };

    class RHIGraphicsShader : public RHIShader
    {
    public:
        explicit RHIGraphicsShader(EShaderFrequency InFrequency) : RHIShader(InFrequency) {}
    };

    class RHIVertexShader : public RHIGraphicsShader
    {
    public:
        RHIVertexShader() : RHIGraphicsShader(SF_Vertex) {}
    };

    class RHIHullShader : public RHIGraphicsShader
    {
    public:
        RHIHullShader() : RHIGraphicsShader(SF_Hull) {}
    };

    class RHIDomainShader : public RHIGraphicsShader
    {
    public:
        RHIDomainShader() : RHIGraphicsShader(SF_Domain) {}
    };

    class RHIPixelShader : public RHIGraphicsShader
    {
    public:
        RHIPixelShader() : RHIGraphicsShader(SF_Pixel) {}
    };

    class RHIGeometryShader : public RHIGraphicsShader
    {
    public:
        RHIGeometryShader() : RHIGraphicsShader(SF_Geometry) {}
    };


    struct BoundShaderStateInput
    {
        inline BoundShaderStateInput() {}

        inline BoundShaderStateInput
        (
            RHIVertexDeclaration* InVertexDeclarationRHI
            , RHIVertexShader* InVertexShaderRHI
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
            , FRHIHullShader* InHullShaderRHI
            , FRHIDomainShader* InDomainShaderRHI
    #endif
            , RHIPixelShader* InPixelShaderRHI
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
            , RHIGeometryShader* InGeometryShaderRHI
    #endif
        )
            : VertexDeclarationRHI(InVertexDeclarationRHI)
            , VertexShaderRHI(InVertexShaderRHI)
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
            , HullShaderRHI(InHullShaderRHI)
            , DomainShaderRHI(InDomainShaderRHI)
    #endif
            , PixelShaderRHI(InPixelShaderRHI)
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
            , GeometryShaderRHI(InGeometryShaderRHI)
    #endif
        {
        }

        RHIVertexDeclaration* VertexDeclarationRHI = nullptr;
        RHIVertexShader* VertexShaderRHI = nullptr;
        RHIHullShader* HullShaderRHI = nullptr;
        RHIDomainShader* DomainShaderRHI = nullptr;
        RHIPixelShader* PixelShaderRHI = nullptr;
        RHIGeometryShader* GeometryShaderRHI = nullptr;
    };

    class RHIGraphicsPipelineState : public RHIResource 
    {
    };
}