#pragma once
#include "core/misc/pch.h"
#include "core/math/math.h"
#include "drivers/rhi/rhi_definitions.h"
#include "drivers/rhi/rhi_inilitializer.h"


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
        RHITexture(uint32_t mips, uint32_t samples, EPixelFormat _format, ETextureCreateFlags _flags, const ClearValueBinding& _clear_value)
        :mips_num(mips)
        ,samples_num(samples)
        ,format(_format)
        ,flags(_flags)
        ,clear_value(_clear_value){}

        virtual class RHITexture2D* cast_texture2d(){return nullptr;}
        virtual class RHITexture2DArray* cast_texture2d_array(){return nullptr;}
        virtual class RHITextureCube* cast_texture_cube(){return nullptr;}
        virtual class RHITexture3D* cast_texture3d(){return nullptr;}

        virtual vec3 get_size_xyz() const = 0;
    public:
        bool is_msaa(){return samples_num > 0;}

        EPixelFormat get_format(){return format;}

        ETextureCreateFlags get_flags(){return flags;}

        uint32_t get_samples_num(){return samples_num;}

        uint32_t get_mips_num(){return mips_num;}

        ClearValueBinding get_clear_value(){return clear_value;}

        void set_texture_name(std::string name){tex_name = name;}
    protected:
        uint32_t mips_num;
        uint32_t samples_num;
        EPixelFormat format;
        ETextureCreateFlags flags;
        ClearValueBinding clear_value;
        std::string tex_name;
    };

    class RHITexture2D : public RHITexture
    {
    public:
        RHITexture2D(uint32_t x, uint32_t y, uint32_t _mips, uint32_t _samples, EPixelFormat _format, ETextureCreateFlags _flags, const ClearValueBinding& _clear_value)
        :RHITexture(_mips,_samples, _format, _flags, _clear_value)
        ,size_x(x)
        ,size_y(y){}

        virtual RHITexture2D* cast_texture2d(){return this;}
        virtual vec2 get_size_xy(){return vec2(size_x, size_y);}
        virtual vec3 get_size_xyz(){return vec3(size_x, size_y, 1.0f);}
    protected:
        uint32_t size_x;
        uint32_t size_y;
    };

    class RHITexture2DArray : public RHITexture2D
    {
    public:
        RHITexture2DArray(uint32_t x, uint32_t y, uint32_t z,uint32_t _mips, uint32_t _samples, EPixelFormat _format, ETextureCreateFlags _flags, const ClearValueBinding& _clear_value)
        :RHITexture2D(x, y,_mips,_samples, _format, _flags, _clear_value)
        ,depth(z){}

        virtual RHITexture2DArray* cast_texture2d_array(){return this;}

        virtual RHITexture2D * cast_texture2d(){return nullptr;}

        virtual vec3 get_size_xyz() const { return vec3(size_x, size_y, depth); }
    protected:
        uint32_t depth;
    };

    class RHITexture3D : public RHITexture
    {
    public:
        RHITexture3D(uint32_t x, uint32_t y, uint32_t z,uint32_t _mips, EPixelFormat _format, ETextureCreateFlags _flags, const ClearValueBinding& _clear_value)
        :RHITexture(_mips, 1, _format, _flags, _clear_value)
        ,size_x(x)
        ,size_y(y)
        ,depth(z){}

        virtual vec3 get_size_xyz() const { return vec3(size_x, size_y, depth); }
        virtual RHITexture3D* cast_texture3d(){return this;}
    protected:
        uint32_t size_x;
        uint32_t size_y;
        uint32_t depth;
    };

    class RHITextureCube : public RHITexture
    {
    public:
        RHITextureCube(uint32_t x, uint32_t _mips, EPixelFormat _format, ETextureCreateFlags _flags, const ClearValueBinding& _clear_value)
        :RHITexture(_mips, 1, _format, _flags, _clear_value)
        ,size(x){}

        virtual vec3 get_size_xyz() const { return vec3(size, size, size); }
        virtual RHITextureCube* cast_texture_cube(){return this;}

    protected:
        uint32_t size;
    };


    //
    // Shader bindings
    //
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

    class RHIGraphicsPipelineState : public RHIResource 
    {
    };


    /** The layout of a uniform buffer in memory. */
    struct RHIUniformBufferLayout
    {
        uint32 const_buffer_size;
    };

    class RHIUniformBuffer : public RHIResource
    {
    public:
        RHIUniformBuffer(const RHIUniformBufferLayout& in_layout)
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
        RHIIndexBuffer(uint32 in_stride,uint32 in_size, EBufferUsageFlags in_usage)
        : stride(in_stride)
        , size(in_size)
        , usage(in_usage)
        {}

        /** @return The stride in bytes of the index buffer; must be 2 or 4. */
        uint32 get_stride() const { return stride; }

        /** @return The number of bytes in the index buffer. */
        uint32 get_size() const { return size; }

        /** @return The usage flags used to create the index buffer. */
        EBufferUsageFlags get_usage() const { return usage; }

        void swap(RHIIndexBuffer& other)
        {
            std::swap(stride, other.stride);
            std::swap(size, other.size);
            std::swap(usage, other.usage);
        }
    private:
        uint32 stride;
        uint32 size;
        EBufferUsageFlags usage;
    };

    class RHIVertexBuffer : public RHIResource
    {
    public:
        RHIVertexBuffer(uint32 in_size, EBufferUsageFlags in_usage)
        : size(in_size)
        , usage(in_usage)
        {}

        /** @return The number of bytes in the vertex buffer. */
        uint32 get_size() const { return size; }

        /** @return The usage flags used to create the vertex buffer. e.g. BUF_UnorderedAccess */
        EBufferUsageFlags get_usage() const { return usage; }

        void swap(RHIVertexBuffer& other)
        {
            std::swap(size, other.size);
            std::swap(usage, other.usage);
        }
    private:
        uint32 size;
        EBufferUsageFlags usage;
    };

    class RHIStructuredBuffer : public RHIResource
    {
    public:
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
    class RHITimestampCalibrationQuery : public RHIResource
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

        virtual bool poll() const final override;

    private:
        uint32 inserted_frame_number;
    };

    class RHIRenderQuery : public RHIResource 
    {};

    //
    // Views
    //

    class RHIUnorderedAccessView : public RHIResource
    {};

    class RHIShaderResourceView : public RHIResource 
    {};

    // Forward declarations for shared pointers
    using RHIShaderRef = std::shared_ptr<RHIShader>;
    using RHIVertexShaderRef = std::shared_ptr<RHIVertexShader>;
    using RHIPixelShaderRef = std::shared_ptr<RHIPixelShader>;
    using RHIHullShaderRef = std::shared_ptr<RHIHullShader>;
    using RHIDomainShaderRef = std::shared_ptr<RHIDomainShader>;
    using RHIGeometryShaderRef = std::shared_ptr<RHIGeometryShader>;

    using RHIBoundShaderStateRef = std::shared_ptr<RHIBoundShaderState>;
    using RHIGraphicsPipelineStateRef = std::shared_ptr<RHIGraphicsPipelineState>;

    using RHITextureRef = std::shared_ptr<RHITexture>;
    using RHITexture2DRef = std::shared_ptr<RHITexture2D>;
    using RHITexture2DArrayRef = std::shared_ptr<RHITexture2DArray>;
    using RHITextureCubeRef = std::shared_ptr<RHITextureCube>;
    using RHITexture3DRef = std::shared_ptr<RHITexture3D>;
    using RHISamplerStateRef = std::shared_ptr<RHISamplerState>;

    using RHIRasterizerStateRef = std::shared_ptr<RHIRasterizerState>;
    using RHIDepthStencilStateRef = std::shared_ptr<RHIDepthStencilState>;
    using RHIBlendStateRef = std::shared_ptr<RHIBlendState>;
    using RHIVertexDeclarationRef = std::shared_ptr<RHIVertexDeclaration>;

    using RHIStructuredBufferRef = std::shared_ptr<RHIStructuredBuffer>;
    using RHIVertexBufferRef = std::shared_ptr<RHIVertexBuffer>;
    using RHIIndexBufferRef = std::shared_ptr<RHIIndexBuffer>;
    using RHIUniformBufferRef = std::shared_ptr<RHIUniformBuffer>;

    using RHIGPUFenceRef = std::shared_ptr<RHIGPUFence>;
    using RHITimestampCalibrationQueryRef = std::shared_ptr<RHITimestampCalibrationQuery>;
    using RHIRenderQueryRef = std::shared_ptr<RHIRenderQuery>;

    using RHIUnorderedAccessViewRef = std::shared_ptr<RHIUnorderedAccessView>;
    using RHIShaderResourceViewRef = std::shared_ptr<RHIShaderResourceView>;


}
