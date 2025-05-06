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
        virtual bool get_initializer(struct RasterizerStateInitializerRHI& init) { return false; }
    };

    class RHIDepthStencilState : public RHIResource
    {
    public:
        virtual bool get_initializer(struct DepthStencilStateInitializerRHI& init) { return false; }
    };

    class RHIBlendState : public RHIResource
    {
    public:
        virtual bool get_initializer(class BlendStateInitializerRHI& init) { return false; }
    };

    class RHISamplerState : public RHIResource 
    {
    public:
        virtual bool is_immutable() const { return false; }
    };

    // Texture base class
    class RHITexture : public RHIResource
    {
    public:
        RHITexture(EPixelFormat _format, ETextureCreateFlags _flags, uint32_t _mips, uint32_t _samples)
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

    class ExclusiveDepthStencil
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
        Type value;

    public:
        // constructor
        ExclusiveDepthStencil(Type in_value = DepthNop_StencilNop)
            : value(in_value)
        {
        }

        inline bool is_using_depth_stencil() const
        {
            return value != DepthNop_StencilNop;
        }
        inline bool is_using_depth() const
        {
            return (extract_depth() != DepthNop);
        }
        inline bool is_using_stencil() const
        {
            return (extract_stencil() != StencilNop);
        }
        inline bool is_depth_write() const
        {
            return extract_depth() == DepthWrite;
        }
        inline bool is_depth_read() const
        {
            return extract_depth() == DepthRead;
        }
        inline bool is_stencil_write() const
        {
            return extract_stencil() == StencilWrite;
        }
        inline bool is_stencil_read() const
        {
            return extract_stencil() == StencilRead;
        }

        inline bool is_any_write() const
        {
            return is_depth_write() || is_stencil_write();
        }

        inline void set_depth_write()
        {
            value = (Type)(extract_stencil() | DepthWrite);
        }
        inline void set_stencil_write()
        {
            value = (Type)(extract_depth() | StencilWrite);
        }
        inline void set_depth_stencil_write(bool b_depth, bool b_stencil)
        {
            value = DepthNop_StencilNop;

            if (b_depth)
            {
                set_depth_write();
            }
            if (b_stencil)
            {
                set_stencil_write();
            }
        }
        bool operator==(const ExclusiveDepthStencil& rhs) const
        {
            return value == rhs.value;
        }

        bool operator != (const ExclusiveDepthStencil& rhs) const
        {
            return value != rhs.value;
        }

        inline bool is_valid(ExclusiveDepthStencil& current) const
        {
            Type depth = extract_depth();

            if (depth != DepthNop && depth != current.extract_depth())
            {
                return false;
            }

            Type stencil = extract_stencil();

            if (stencil != StencilNop && stencil != current.extract_stencil())
            {
                return false;
            }

            return true;
        }

        inline void get_access(ERHIAccess& depth_access, ERHIAccess& stencil_access) const
        {
            depth_access = ERHIAccess::None;

            // SRV access is allowed whilst a depth stencil target is "readable".
            ERHIAccess dsv_read_only_mask = static_cast<ERHIAccess>(
                static_cast<uint32>(ERHIAccess::DSVRead) | 
                static_cast<uint32>(ERHIAccess::SRVGraphics) | 
                static_cast<uint32>(ERHIAccess::SRVCompute)
            );

            // If write access is required, only the depth block can access the resource.
            ERHIAccess dsv_read_write_mask = static_cast<ERHIAccess>(
                static_cast<uint32>(ERHIAccess::DSVRead) | 
                static_cast<uint32>(ERHIAccess::DSVWrite)
            );

            if (is_using_depth())
            {
                depth_access = is_depth_write() ? dsv_read_write_mask : dsv_read_only_mask;
            }

            stencil_access = ERHIAccess::None;

            if (is_using_stencil())
            {
                stencil_access = is_stencil_write() ? dsv_read_write_mask : dsv_read_only_mask;
            }
        }

        template <typename TFunction>
        inline void enumerate_subresources(TFunction function) const
        {
            if (!is_using_depth_stencil())
            {
                return;
            }

            ERHIAccess depth_access = ERHIAccess::None;
            ERHIAccess stencil_access = ERHIAccess::None;
            get_access(depth_access, stencil_access);

            // Same depth / stencil state; single subresource.
            if (depth_access == stencil_access)
            {
                function(depth_access, FRHITransitionInfo::kAllSubresources);
            }
            // Separate subresources for depth / stencil.
            else
            {
                if (depth_access != ERHIAccess::None)
                {
                    function(depth_access, FRHITransitionInfo::kDepthPlaneSlice);
                }
                if (stencil_access != ERHIAccess::None)
                {
                    function(stencil_access, FRHITransitionInfo::kStencilPlaneSlice);
                }
            }
        }

        inline ExclusiveDepthStencil get_readable_transition() const
        {
            ExclusiveDepthStencil::Type new_depth_state = is_depth_write()
                ? ExclusiveDepthStencil::DepthRead
                : ExclusiveDepthStencil::DepthNop;

            ExclusiveDepthStencil::Type new_stencil_state = is_stencil_write()
                ? ExclusiveDepthStencil::StencilRead
                : ExclusiveDepthStencil::StencilNop;

            return (ExclusiveDepthStencil::Type)(new_depth_state | new_stencil_state);
        }

        inline ExclusiveDepthStencil get_writable_transition() const
        {
            ExclusiveDepthStencil::Type new_depth_state = is_depth_read()
                ? ExclusiveDepthStencil::DepthWrite
                : ExclusiveDepthStencil::DepthNop;

            ExclusiveDepthStencil::Type new_stencil_state = is_stencil_read()
                ? ExclusiveDepthStencil::StencilWrite
                : ExclusiveDepthStencil::StencilNop;

            return (ExclusiveDepthStencil::Type)(new_depth_state | new_stencil_state);
        }

        uint32 get_index() const
        {
            switch (value)
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
        inline Type extract_depth() const
        {
            return (Type)(value & DepthMask);
        }
        inline Type extract_stencil() const
        {
            return (Type)(value & StencilMask);
        }
    };

    //
    // Shader bindings
    //

    typedef std::array<struct VertexElement, MaxVertexElementCount> VertexDeclarationElementList;
    class RHIVertexDeclaration : public RHIResource
    {
    public:
        virtual bool get_initializer(VertexDeclarationElementList& init) { return false; }
    };

    class RHIBoundShaderState : public RHIResource {};

    //
    // Shaders
    //

    class RHIShader : public RHIResource
    {
    public:
        void set_hash(std::size_t in_hash) { hash = in_hash; }
        std::size_t get_hash() const { return hash; }

        explicit RHIShader(EShaderFrequency in_frequency)
            : frequency(in_frequency)
        {
        }

        inline EShaderFrequency get_frequency() const
        {
            return frequency;
        }

    #if (BUILD_DEBUG || BUILD_DEVELOPMENT)
        std::string shader_name;
        const std::string get_shader_name() const { return shader_name; }
    #else
        const std::string get_shader_name() const { return ""; }
    #endif
    private:
        std::size_t hash;
        EShaderFrequency frequency;
    };

    class RHIGraphicsShader : public RHIShader
    {
    public:
        explicit RHIGraphicsShader(EShaderFrequency in_frequency) : RHIShader(in_frequency) {}
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
            RHIVertexDeclaration* in_vertex_declaration_rhi
            , RHIVertexShader* in_vertex_shader_rhi
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
            , FRHIHullShader* in_hull_shader_rhi
            , FRHIDomainShader* in_domain_shader_rhi
    #endif
            , RHIPixelShader* in_pixel_shader_rhi
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
            , RHIGeometryShader* in_geometry_shader_rhi
    #endif
        )
            : vertex_declaration_rhi(in_vertex_declaration_rhi)
            , vertex_shader_rhi(in_vertex_shader_rhi)
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
            , hull_shader_rhi(in_hull_shader_rhi)
            , domain_shader_rhi(in_domain_shader_rhi)
    #endif
            , pixel_shader_rhi(in_pixel_shader_rhi)
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
            , geometry_shader_rhi(in_geometry_shader_rhi)
    #endif
        {
        }

        RHIVertexDeclaration* vertex_declaration_rhi = nullptr;
        RHIVertexShader* vertex_shader_rhi = nullptr;
        RHIHullShader* hull_shader_rhi = nullptr;
        RHIDomainShader* domain_shader_rhi = nullptr;
        RHIPixelShader* pixel_shader_rhi = nullptr;
        RHIGeometryShader* geometry_shader_rhi = nullptr;
    };

    class RHIGraphicsPipelineState : public RHIResource 
    {
    };


    /** The layout of a uniform buffer in memory. */
    struct RHIUniformBufferLayout
    {
        uint32 const_buffer_size;
    };

    class FRHIUniformBuffer : public RHIResource
    {
    public:

        /** Initialization constructor. */
        FRHIUniformBuffer(const RHIUniformBufferLayout& in_layout)
        : layout(&in_layout)
        , layout_const_buffer_size(in_layout.const_buffer_size)
        {}

        uint32 get_size() const
        {
            return layout_const_buffer_size;
        }
        const RHIUniformBufferLayout& get_layout() const { return *layout; }
    private:
        /** Layout of the uniform buffer. */
        const RHIUniformBufferLayout* layout;

        uint32 layout_const_buffer_size;
    };

    class RHIIndexBuffer : public RHIResource
    {
    public:

        /** Initialization constructor. */
        RHIIndexBuffer(uint32 in_stride,uint32 in_size,uint32 in_usage)
        : stride(in_stride)
        , size(in_size)
        , usage(in_usage)
        {}

        /** @return The stride in bytes of the index buffer; must be 2 or 4. */
        uint32 get_stride() const { return stride; }

        /** @return The number of bytes in the index buffer. */
        uint32 get_size() const { return size; }

        /** @return The usage flags used to create the index buffer. */
        uint32 get_usage() const { return usage; }

    protected:
        RHIIndexBuffer()
            : stride(0)
            , size(0)
            , usage(0)
        {}

        void swap(RHIIndexBuffer& other)
        {
            std::swap(stride, other.stride);
            std::swap(size, other.size);
            std::swap(usage, other.usage);
        }

        void release_underlying_resource()
        {
            stride = size = usage = 0;
        }

    private:
        uint32 stride;
        uint32 size;
        uint32 usage;
    };

    class RHIVertexBuffer : public RHIResource
    {
    public:

        /**
         * Initialization constructor.
         * @apram in_usage e.g. BUF_UnorderedAccess
         */
        RHIVertexBuffer(uint32 in_size, uint32 in_usage)
        : size(in_size)
        , usage(in_usage)
        {}

        /** @return The number of bytes in the vertex buffer. */
        uint32 get_size() const { return size; }

        /** @return The usage flags used to create the vertex buffer. e.g. BUF_UnorderedAccess */
        uint32 get_usage() const { return usage; }

    protected:
        RHIVertexBuffer()
            : size(0)
            , usage(0)
        {}

        void swap(RHIVertexBuffer& other)
        {
            std::swap(size, other.size);
            std::swap(usage, other.usage);
        }

        void release_underlying_resource()
        {
            size = 0;
            usage = 0;
        }

    private:
        uint32 size;
        uint32 usage;
    };

    class RHIStructuredBuffer : public RHIResource
    {
    public:

        /** Initialization constructor. */
        RHIStructuredBuffer(uint32 in_stride,uint32 in_size, uint32 in_usage)
        : stride(in_stride)
        , size(in_size)
        , usage(in_usage)
        {}

        /** @return The stride in bytes of the structured buffer; must be 2 or 4. */
        uint32 get_stride() const { return stride; }

        /** @return The number of bytes in the structured buffer. */
        uint32 get_size() const { return size; }

        /** @return The usage flags used to create the structured buffer. */
        uint32 get_usage() const { return usage; }

    private:
        uint32 stride;
        uint32 size;
        uint32 usage;
    };

    //
    // Misc
    //
    class FRHITimestampCalibrationQuery : public RHIResource
    {
    public:
        uint64 gpu_microseconds = 0;
        uint64 cpu_microseconds = 0;
    };

    class RHIGPUFence : public RHIResource
    {
    public:
        RHIGPUFence(std::string in_name) : fence_name(in_name) {}
        virtual ~RHIGPUFence() {}

        virtual void clear() = 0;

        /**
         * Poll the fence to see if the GPU has signaled it.
         * @returns True if and only if the GPU fence has been inserted and the GPU has signaled the fence.
         */
        virtual bool poll() const = 0;

        const std::string& get_name() const { return fence_name; }

    protected:
        std::string fence_name;
    };

    // Generic implementation of RHIGPUFence
    class GenericRHIGPUFence : public RHIGPUFence
    {
    public:
        GenericRHIGPUFence(std::string in_name);

        virtual void clear() final override;

        /** @discussion RHI implementations must be thread-safe and must correctly handle being called before RHIInsertFence if an RHI thread is active. */
        virtual bool poll() const final override;

    private:
        uint32 inserted_frame_number;
    };

    class RHIRenderQuery : public RHIResource {};

}
