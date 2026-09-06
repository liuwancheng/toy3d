#pragma once

#include "misc/enum_class_utils.h"
#include "pixel_format/pixel_format.h"

#include <cstdint>

namespace toy3d
{
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
    ENUM_CLASS_FLAGS(RHIResourceUsage)

    enum class RHICPUAccess : std::uint8_t
    {
        // The resource is not directly CPU accessible. Updates and readback
        // use explicit GPU copy paths.
        None,
        // The CPU consumes GPU-produced data through a backend readback path.
        Read,
        // The CPU supplies data through a backend upload or dynamic-resource path.
        Write
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
        CPUWrite = 1U << 18
    };
    ENUM_CLASS_FLAGS(RHIAccess)

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
    ENUM_CLASS_FLAGS(RHIShaderStageFlags)

    enum class RHIBindingGroup : std::uint8_t
    {
        Global,
        View,
        Pass,
        Material,
        Object,
        Max
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

    enum class RHIFilter : std::uint8_t
    {
        Nearest,
        Linear
    };

    enum class RHIAddressMode : std::uint8_t
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    enum class RHIBorderColor : std::uint8_t
    {
        TransparentBlack,
        OpaqueBlack,
        OpaqueWhite
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

    enum class RHIVertexInputRate : std::uint8_t
    {
        PerVertex,
        PerInstance
    };

    enum class RHIPolygonMode : std::uint8_t
    {
        Fill,
        Line,
        Point
    };

    enum class RHICullMode : std::uint8_t
    {
        None,
        Front,
        Back
    };

    enum class RHIFrontFace : std::uint8_t
    {
        CounterClockwise,
        Clockwise
    };

    enum class RHICompareOperation : std::uint8_t
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    enum class RHIStencilOperation : std::uint8_t
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap
    };

    enum class RHIBlendFactor : std::uint8_t
    {
        Zero,
        One,
        SourceColor,
        OneMinusSourceColor,
        DestinationColor,
        OneMinusDestinationColor,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationAlpha,
        OneMinusDestinationAlpha,
        ConstantColor,
        OneMinusConstantColor,
        SourceAlphaSaturate
    };

    enum class RHIBlendOperation : std::uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    enum class RHIColorWriteMask : std::uint8_t
    {
        None = 0,
        Red = 1U << 0,
        Green = 1U << 1,
        Blue = 1U << 2,
        Alpha = 1U << 3,
        All = Red | Green | Blue | Alpha
    };
    ENUM_CLASS_FLAGS(RHIColorWriteMask)

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

    using RHIQueueCompletionValue = std::uint64_t;

    constexpr std::uint32_t RHI_ALL_MIPS = 0xffffffffU;
    constexpr std::uint32_t RHI_ALL_LAYERS = 0xffffffffU;
    constexpr std::uint32_t RHI_MAX_COLOR_ATTACHMENTS = 8U;
} // namespace toy3d
