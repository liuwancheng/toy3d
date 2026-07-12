#pragma once

#include <cstdint>
#include <type_traits>

namespace toy3d
{
    template<typename T>
    constexpr T rhi_enum_or(T left, T right)
    {
        static_assert(std::is_enum<T>::value, "RHI flag type must be an enum.");
        using Underlying = typename std::underlying_type<T>::type;
        return static_cast<T>(static_cast<Underlying>(left) | static_cast<Underlying>(right));
    }

    template<typename T>
    constexpr bool rhi_has_any_flag(T value, T flags)
    {
        static_assert(std::is_enum<T>::value, "RHI flag type must be an enum.");
        using Underlying = typename std::underlying_type<T>::type;
        return (static_cast<Underlying>(value) & static_cast<Underlying>(flags)) != 0;
    }

    enum class RHIFormat : std::uint16_t
    {
        Unknown,
        R8UNorm,
        R8G8B8A8UNorm,
        R8G8B8A8UNormSRGB,
        B8G8R8A8UNorm,
        B8G8R8A8UNormSRGB,
        R16Float,
        R16G16Float,
        R16G16B16A16Float,
        R32Float,
        R32G32Float,
        R32G32B32Float,
        R32G32B32A32Float,
        R16UInt,
        R32UInt,
        R8SNorm,
        R8G8B8A8SNorm,
        R10G10B10A2UNorm,
        R11G11B10Float,
        BC1UNorm,
        BC2UNorm,
        BC3UNorm,
        UYVY,
        PVRTC2,
        PVRTC4,
        ASTC4x4,
        ASTC6x6,
        ASTC8x8,
        ASTC12x12,
        HDR,
        D16UNorm,
        D24UNormS8UInt,
        D32Float,
        D32FloatS8UInt,

        // Legacy spelling aliases. Remove after backend and RenderScene migration.
        Unknow = Unknown,
        A32B32G32R32F = R32G32B32A32Float,
        B8G8R8A8 = B8G8R8A8UNorm,
        G8 = R8UNorm,
        G16 = R16UInt,
        DXT1 = BC1UNorm,
        DXT3 = BC2UNorm,
        DXT5 = BC3UNorm,
        FloatRGB = R32G32B32Float,
        FloatRGBA = R16G16B16A16Float,
        DepthStencil = D24UNormS8UInt,
        ShadowDepth = D32Float,
        R32_Float = R32Float,
        G16R16 = R16G16Float,
        G16R16F = R16G16Float,
        G32R32F = R32G32Float,
        A2B10G10R10 = R10G10B10A2UNorm,
        A16G16B16R16 = R16G16B16A16Float,
        R16G16B16A16 = R16G16B16A16Float,
        Depth24 = D24UNormS8UInt,
        FloatR11G11B10 = R11G11B10Float,
        A8 = R8UNorm,
        R32_UINT = R32UInt,
        R8G8B8A8 = R8G8B8A8UNorm,
        A8R8G8B8 = B8G8R8A8UNorm,
        R8G8B8A8_SNORM = R8G8B8A8SNorm,
        PF_R8G8B8A8_UNORM = R8G8B8A8UNorm,
        ASTC_4x4 = ASTC4x4,
        ASTC_6x6 = ASTC6x6,
        ASTC_8x8 = ASTC8x8,
        ASTC_12x12 = ASTC12x12,
        PixelFormat_Max
    };

    enum class RHIResourceDimension : std::uint8_t
    {
        Buffer,
        Texture1D,
        Texture2D,
        Texture3D
    };

    enum class RHITextureViewDimension : std::uint8_t
    {
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D,
        TextureCube,
        TextureCubeArray
    };

    enum class RHIResourceUsage : std::uint32_t
    {
        None = 0,
        VertexBuffer = 1U << 0,
        IndexBuffer = 1U << 1,
        UniformBuffer = 1U << 2,
        ShaderResource = 1U << 3,
        UnorderedAccess = 1U << 4,
        RenderTarget = 1U << 5,
        DepthStencil = 1U << 6,
        IndirectArguments = 1U << 7,
        CopySource = 1U << 8,
        CopyDestination = 1U << 9
    };

    enum class RHICPUAccess : std::uint8_t
    {
        None,
        Read,
        Write,
        ReadWrite
    };

    enum class RHIAccess : std::uint32_t
    {
        Unknown = 0,
        Common = 1U << 0,
        Present = 1U << 1,
        VertexBuffer = 1U << 2,
        IndexBuffer = 1U << 3,
        UniformBuffer = 1U << 4,
        IndirectArguments = 1U << 5,
        ShaderResourceGraphics = 1U << 6,
        ShaderResourceCompute = 1U << 7,
        UnorderedAccessGraphics = 1U << 8,
        UnorderedAccessCompute = 1U << 9,
        RenderTarget = 1U << 10,
        DepthStencilRead = 1U << 11,
        DepthStencilWrite = 1U << 12,
        CopySource = 1U << 13,
        CopyDestination = 1U << 14,
        ResolveSource = 1U << 15,
        ResolveDestination = 1U << 16,
        CPURead = 1U << 17,
        CPUWrite = 1U << 18,

        // Legacy spelling aliases. Remove after command-context migration.
        None = Unknown,
        IndirectArgs = IndirectArguments,
        VertexOrIndexBuffer = (1U << 2) | (1U << 3),
        SRVCompute = ShaderResourceCompute,
        SRVGraphics = ShaderResourceGraphics,
        UAVCompute = UnorderedAccessCompute,
        UAVGraphics = UnorderedAccessGraphics,
        RTV = RenderTarget,
        DSVRead = DepthStencilRead,
        DSVWrite = DepthStencilWrite,
        CopySrc = CopySource,
        CopyDest = CopyDestination,
        ResolveSrc = ResolveSource,
        ResolveDst = ResolveDestination,
        EReadable = (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) |
            (1U << 5) | (1U << 6) | (1U << 7) | (1U << 11) |
            (1U << 13) | (1U << 15) | (1U << 17),
        EWritable = (1U << 8) | (1U << 9) | (1U << 10) | (1U << 12) |
            (1U << 14) | (1U << 16) | (1U << 18)
    };

    enum class RHIShaderStage : std::uint8_t
    {
        Vertex,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute
    };

    enum class RHIShaderStageFlags : std::uint32_t
    {
        None = 0,
        Vertex = 1U << 0,
        Pixel = 1U << 1,
        Geometry = 1U << 2,
        Hull = 1U << 3,
        Domain = 1U << 4,
        Compute = 1U << 5,
        AllGraphics = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4),
        All = AllGraphics | (1U << 5)
    };

    enum class RHIBindingGroup : std::uint8_t
    {
        Global,
        View,
        Pass,
        Material,
        Object
    };

    enum class RHIResourceBindingType : std::uint8_t
    {
        UniformBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        ReadOnlyBuffer,
        StorageBuffer
    };

    enum class RHITextureAspect : std::uint8_t
    {
        Color,
        Depth,
        Stencil,
        DepthStencil
    };

    enum class RHIResourceViewType : std::uint8_t
    {
        ShaderResource,
        UnorderedAccess,
        RenderTarget,
        DepthStencil
    };

    enum class RHILoadOperation : std::uint8_t
    {
        Load,
        Clear,
        Discard
    };

    enum class RHIStoreOperation : std::uint8_t
    {
        Store,
        Discard
    };

    enum class RHIPrimitiveTopology : std::uint8_t
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    enum class RHIIndexFormat : std::uint8_t
    {
        UInt16,
        UInt32
    };

    enum class RHICommandListState : std::uint8_t
    {
        Initial,
        Recording,
        Closed,
        Submitted
    };

    enum class RHIPresentMode : std::uint8_t
    {
        Immediate,
        Mailbox,
        Fifo
    };

    using RHISubmitSerial = std::uint64_t;

    constexpr std::uint32_t RHI_ALL_MIPS = 0xffffffffU;
    constexpr std::uint32_t RHI_ALL_LAYERS = 0xffffffffU;
}
