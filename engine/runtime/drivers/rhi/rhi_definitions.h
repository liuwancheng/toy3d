#pragma once
#include "core/misc/pch.h"
#include "core/misc/enum_class_utils.h"

namespace  toy3d
{
/** Maximum number of miplevels in a texture. */
enum { MAX_TEXTURE_MIP_COUNT = 15 };

/** Maximum number of static/skeletal mesh LODs */
enum { MAX_MESH_LOD_COUNT = 8 };

/** Maximum number of immutable samplers in a PSO. */
enum { MaxImmutableSamplers = 2 };

/** The maximum number of vertex elements which can be used by a vertex declaration. */
enum { MaxVertexElementCount = 16 };

/** The alignment in bytes between elements of array shader parameters. */
enum { ShaderArrayElementAlignBytes = 16 };

/** The number of render-targets that may be simultaneously written to. */
enum { MaxSimultaneousRenderTargets = 8 };

/** The number of UAVs that may be simultaneously bound to a shader. */
enum { MaxSimultaneousUAVs = 8 };

/** The maximum number of static slots allowed. */
enum { MAX_UNIFORM_BUFFER_STATIC_SLOTS = 255 };

enum ERHIShadingPath : uint8
{
	Deferred,
	Forward
};

enum EShaderFrequency : uint8
{
	SF_Vertex			= 0,
	SF_Hull				= 1,
	SF_Domain			= 2,
	SF_Pixel			= 3,
	SF_Geometry			= 4,
	SF_Compute			= 5,
	SF_RayGen			= 6,
	SF_RayMiss			= 7,
	SF_RayHitGroup		= 8,
	SF_RayCallable		= 9,

	SF_NumFrequencies	= 10,
	SF_NumGraphicsFrequencies = 5,
	SF_NumStandardFrequencies = 6
};

enum ESamplerFilter : uint8
{
	SF_Point,
	SF_Bilinear,
	SF_Trilinear,
	SF_AnisotropicPoint,
	SF_AnisotropicLinear,

	ESamplerFilter_Max
};

enum ESamplerAddressMode : uint8
{
	AM_Wrap,
	AM_Clamp,
	AM_Mirror,
	AM_Border,

	ESamplerAddressMode_Max
};

enum ESamplerCompareFunction : uint8
{
	SCF_Never,
	SCF_Less
};

enum ERasterizerFillMode : uint8
{
	FM_Point,
	FM_Wireframe,
	FM_Solid,

	ERasterizerFillMode_Max
};

enum ERasterizerCullMode : uint8
{
	CM_None,
	CM_CW,
	CM_CCW,

	ERasterizerCullMode_Max
};

enum EColorWriteMask
{
	CW_RED   = 0x01,
	CW_GREEN = 0x02,
	CW_BLUE  = 0x04,
	CW_ALPHA = 0x08,

	CW_NONE  = 0,
	CW_RGB   = CW_RED | CW_GREEN | CW_BLUE,
	CW_RGBA  = CW_RED | CW_GREEN | CW_BLUE | CW_ALPHA,
	CW_RG    = CW_RED | CW_GREEN,
	CW_BA    = CW_BLUE | CW_ALPHA
};

enum ECompareFunction : uint8
{
	CF_Less,
	CF_LessEqual,
	CF_Greater,
	CF_GreaterEqual,
	CF_Equal,
	CF_NotEqual,
	CF_Never,
	CF_Always,

	ECompareFunction_Max
};

enum EStencilMask : uint8
{
	SM_Default,
	SM_255,
	SM_1,
	SM_2,
	SM_4,
	SM_8,
	SM_16,
	SM_32,
	SM_64,
	SM_128,
	SM_Max
};

enum EStencilOp : uint8
{
	SO_Keep,
	SO_Zero,
	SO_Replace,
	SO_SaturatedIncrement,
	SO_SaturatedDecrement,
	SO_Invert,
	SO_Increment,
	SO_Decrement,

	EStencilOp_Max
};

enum EBlendOperation : uint8
{
	BO_Add,
	BO_Subtract,
	BO_Min,
	BO_Max,
	BO_ReverseSubtract,

	EBlendOperation_Max
};

enum EBlendFactor : uint8
{
	BF_Zero,
	BF_One,
	BF_SourceColor,
	BF_InverseSourceColor,
	BF_SourceAlpha,
	BF_InverseSourceAlpha,
	BF_DestAlpha,
	BF_InverseDestAlpha,
	BF_DestColor,
	BF_InverseDestColor,
	BF_ConstantBlendFactor,
	BF_InverseConstantBlendFactor,
	BF_Source1Color,
	BF_InverseSource1Color,
	BF_Source1Alpha,
	BF_InverseSource1Alpha,

	EBlendFactor_Max
};

enum EVertexElementType : uint8
{
	VET_None,
	VET_Float1,
	VET_Float2,
	VET_Float3,
	VET_Float4,
	VET_PackedNormal,	// FPackedNormal
	VET_UByte4,
	VET_UByte4N,
	VET_Color,
	VET_Short2,
	VET_Short4,
	VET_Short2N,		// 16 bit word normalized to (value/32767.0,value/32767.0,0,0,1)
	VET_Half2,			// 16 bit float using 1 bit sign, 5 bit exponent, 10 bit mantissa 
	VET_Half4,
	VET_Short4N,		// 4 X 16 bit word, normalized 
	VET_UShort2,
	VET_UShort4,
	VET_UShort2N,		// 16 bit word normalized to (value/65535.0,value/65535.0,0,0,1)
	VET_UShort4N,		// 4 X 16 bit word unsigned, normalized 
	VET_URGB10A2N,		// 10 bit r, g, b and 2 bit a normalized to (value/1023.0f, value/1023.0f, value/1023.0f, value/3.0f)
	VET_UInt,
	VET_Max
};

enum ECubeFace : uint8
{
	CubeFace_PosX = 0,
	CubeFace_NegX,
	CubeFace_PosY,
	CubeFace_NegY,
	CubeFace_PosZ,
	CubeFace_NegZ,
	CubeFace_Max
};

enum EUniformBufferUsage : uint8
{
	UniformBuffer_SingleDraw = 0,
	UniformBuffer_SingleFrame,
	UniformBuffer_MultiFrame,
};

enum EResourceLockMode
{
	RLM_ReadOnly,
	RLM_WriteOnly,
	RLM_WriteOnly_NoOverwrite,
	RLM_Max
};

enum ERangeCompressionMode
{
	// 0 .. 1
	RCM_UNorm,
	// -1 .. 1
	RCM_SNorm,
	// 0 .. 1 unless there are smaller values than 0 or bigger values than 1, then the range is extended to the minimum or the maximum of the values
	RCM_MinMaxNorm,
	// minimum .. maximum (each channel independent)
	RCM_MinMax,
};

enum EPrimitiveType
{
	// Topology that defines a triangle N with 3 vertex extremities: 3*N+0, 3*N+1, 3*N+2.
	PT_TriangleList,

	// Topology that defines a triangle N with 3 vertex extremities: N+0, N+1, N+2.
	PT_TriangleStrip,

	// Topology that defines a line with 2 vertex extremities: 2*N+0, 2*N+1.
	PT_LineList,

	// Topology that defines a quad N with 4 vertex extremities: 4*N+0, 4*N+1, 4*N+2, 4*N+3.
	// Supported only if GRHISupportsQuadTopology == true.
	PT_QuadList,

	// Topology that defines a point N with a single vertex N.
	PT_PointList,

	// Topology that defines a screen aligned rectangle N with only 3 vertex corners:
	//    3*N + 0 is upper-left corner,
	//    3*N + 1 is upper-right corner,
	//    3*N + 2 is the lower-left corner.
	// Supported only if GRHISupportsRectTopology == true.
	PT_RectList,

	// Tesselation patch list. Supported only if tesselation is supported.
	PT_1_ControlPointPatchList,
	PT_2_ControlPointPatchList,
	PT_3_ControlPointPatchList,
	PT_4_ControlPointPatchList,
	PT_5_ControlPointPatchList,
	PT_6_ControlPointPatchList,
	PT_7_ControlPointPatchList,
	PT_8_ControlPointPatchList,
	PT_9_ControlPointPatchList,
	PT_10_ControlPointPatchList,
	PT_11_ControlPointPatchList,
	PT_12_ControlPointPatchList,
	PT_13_ControlPointPatchList,
	PT_14_ControlPointPatchList,
	PT_15_ControlPointPatchList,
	PT_16_ControlPointPatchList,
	PT_17_ControlPointPatchList,
	PT_18_ControlPointPatchList,
	PT_19_ControlPointPatchList,
	PT_20_ControlPointPatchList,
	PT_21_ControlPointPatchList,
	PT_22_ControlPointPatchList,
	PT_23_ControlPointPatchList,
	PT_24_ControlPointPatchList,
	PT_25_ControlPointPatchList,
	PT_26_ControlPointPatchList,
	PT_27_ControlPointPatchList,
	PT_28_ControlPointPatchList,
	PT_29_ControlPointPatchList,
	PT_30_ControlPointPatchList,
	PT_31_ControlPointPatchList,
	PT_32_ControlPointPatchList,
	PT_Max,
	PT_MaxBits = 6
};

enum class EBufferUsageFlags
{
	BUF_None					= 0x0000,
	BUF_Static					= 0x0001, // The buffer will be written to once, GPU read only, CPU write only.  The data lifetime is until the buffer is destroyed.
	BUF_Dynamic					= 0x0002, // GPU read only, CPU write only.
	BUF_Volatile				= 0x0004, // The buffer's data will have a lifetime of one frame.  It MUST be written to each frame, or a new one created each frame.
	// Mutually exclusive bind flags.
	BUF_UnorderedAccess			= 0x0008, // Allows an unordered access view to be created for the buffer.
	BUF_DrawIndirect			= 0x0100,
	BUF_ShaderResource			= 0x0200,
	BUF_Transient				= 0x2000, // Buffer should be allocated from transient memory.

	BUF_VertexBuffer			= 0x10000,
	BUF_IndexBuffer				= 0x20000,
	BUF_StructuredBuffer		= 0x40000,

	// Helper bit-masks
	BUF_AnyDynamic = (BUF_Dynamic | BUF_Volatile),
};
ENUM_CLASS_FLAGS(EBufferUsageFlags);

enum class ETextureDimension
{
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray
};

enum class EClearDepthStencil
{
	Depth,
	Stencil,
	DepthStencil,
};

enum class ERenderTargetLoadAction : uint8
{
    ENoAction,
    ELoad,
    EClear,
    EDontCare
};

enum class ERenderTargetStoreAction : uint8
{
    ENoAction,
    EStore,
    EDontCare,
    EMultisampleResolve
};

enum class ESubpassHint : uint8
{
    None,
    DepthReadSubpass,       // subpass need read depth
    ColorReadSubpass,       // subpass need read scene color
    DeferredShadingSubpass  // mobile defferred shading subpass
};

enum class EPixelFormat : uint8
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
    PixelFormat_Max
};

enum class ETextureCreateFlags : uint32
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
	Tex_UAV 					= 1<<8,
    Tex_Transient               = 1<<9     // 临时申请的资源
};
ENUM_CLASS_FLAGS(ETextureCreateFlags);

enum class ERHIPipeline : uint8_t
{
    Graphics = 1 << 0,
    AsyncCompute = 1 << 1,

    All = Graphics | AsyncCompute,
    Num = 2
};

enum class ERHIAccess : uint32
{
    Unknown = 0,

    // Read states
    CPURead             = 1 <<  0,
    Present             = 1 <<  1,
    IndirectArgs        = 1 <<  2,
    VertexOrIndexBuffer = 1 <<  3,
    SRVCompute          = 1 <<  4,
    SRVGraphics         = 1 <<  5,
    CopySrc             = 1 <<  6,
    ResolveSrc          = 1 <<  7,
    DSVRead				= 1 <<  8,

    // Read-write states
    UAVCompute          = 1 <<  9,
    UAVGraphics         = 1 << 10,
    RTV                 = 1 << 11,
    CopyDest            = 1 << 12,
    ResolveDst          = 1 << 13,
    DSVWrite            = 1 << 14,

    ShadingRateSource	= 1 << 15,

    Last = DSVWrite,
    None = Unknown,
    Mask = (Last << 1) - 1,

    // A mask of the two possible SRV states
    SRVMask = SRVCompute | SRVGraphics,

    // A mask of the two possible UAV states
    UAVMask = UAVCompute | UAVGraphics,

    // A mask of all bits representing read-only states which cannot be combined with other write states.
    ReadOnlyExclusiveMask = CPURead | Present | IndirectArgs | VertexOrIndexBuffer | SRVGraphics | SRVCompute | CopySrc | ResolveSrc,

    // A mask of all bits representing read-only states which may be combined with other write states.
    ReadOnlyMask = ReadOnlyExclusiveMask | DSVRead | ShadingRateSource,

    // A mask of all bits representing readable states which may also include writable states.
    ReadableMask = ReadOnlyMask | UAVMask,

    // A mask of all bits representing write-only states which cannot be combined with other read states.
    WriteOnlyExclusiveMask = RTV | CopyDest | ResolveDst,

    // A mask of all bits representing write-only states which may be combined with other read states.
    WriteOnlyMask = WriteOnlyExclusiveMask | DSVWrite,

    // A mask of all bits representing writable states which may also include readable states.
    WritableMask = WriteOnlyMask | UAVMask,

    EReadable    = ReadOnlyMask,
    EWritable    = WritableMask, 
    ERWBarrier   = CopySrc | CopyDest | SRVCompute | SRVGraphics | UAVCompute | UAVGraphics,
    ERWNoBarrier = ERWBarrier
};

} // namespace  toy3d


