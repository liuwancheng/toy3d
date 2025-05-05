#pragma once
#include "core/misc/pch.h"
#include "core/math/math.h"
#include "rhi_definitions.h"

namespace toy3d
{

    /** Info for supporting the vertex element types */
    class VertexElementTypeSupportInfo
    {
    public:
        VertexElementTypeSupportInfo() { for(int32 i=0; i<VET_Max; i++) ElementCaps[i]=true; }
        bool IsSupported(EVertexElementType ElementType) { return ElementCaps[ElementType]; }
        void SetSupported(EVertexElementType ElementType,bool bIsSupported) { ElementCaps[ElementType]=bIsSupported; }
    private:
        /** cap bit set for each VET. One-to-one mapping based on EVertexElementType */
        bool ElementCaps[VET_Max];
    };

    struct VertexElement
    {
        uint8_t StreamIndex;
        uint8_t Offset;
        EVertexElementType Type;
        uint8_t AttributeIndex;
        uint16 Stride;
        /**
         * Whether to use instance index or vertex index to consume the element.  
         * eg if bUseInstanceIndex is 0, the element will be repeated for every instance.
         */
        uint16 bUseInstanceIndex;

        VertexElement() {}
        VertexElement(uint8_t InStreamIndex,uint8_t InOffset,EVertexElementType InType,uint8_t InAttributeIndex,uint16 InStride,bool bInUseInstanceIndex = false):
            StreamIndex(InStreamIndex),
            Offset(InOffset),
            Type(InType),
            AttributeIndex(InAttributeIndex),
            Stride(InStride),
            bUseInstanceIndex(bInUseInstanceIndex)
        {}
        /**
        * Suppress the compiler generated assignment operator so that padding won't be copied.
        * This is necessary to get expected results for code that zeros, assigns and then CRC's the whole struct.
        */
        void operator=(const VertexElement& Other)
        {
            StreamIndex = Other.StreamIndex;
            Offset = Other.Offset;
            Type = Other.Type;
            AttributeIndex = Other.AttributeIndex;
            Stride = Other.Stride;
            bUseInstanceIndex = Other.bUseInstanceIndex;
        }
    };

    typedef std::vector<VertexElement> VertexDeclarationElementList;

    /** RHI representation of a single stream out element. */
    //#todo-RemoveStreamOut
    struct FStreamOutElement
    {
        /** Index of the output stream from the geometry shader. */
        uint32_t Stream;

        /** Semantic name of the output element as defined in the geometry shader.  This should not contain the semantic number. */
        const char* SemanticName;

        /** Semantic index of the output element as defined in the geometry shader.  For example "TEXCOORD5" in the shader would give a SemanticIndex of 5. */
        uint32_t SemanticIndex;

        /** Start component index of the shader output element to stream out. */
        uint8_t StartComponent;

        /** Number of components of the shader output element to stream out. */
        uint8_t ComponentCount;

        /** Stream output target slot, corresponding to the streams set by RHISetStreamOutTargets. */
        uint8_t OutputSlot;

        FStreamOutElement() {}
        FStreamOutElement(uint32_t InStream, const char* InSemanticName, uint32_t InSemanticIndex, uint8_t InComponentCount, uint8_t InOutputSlot) :
            Stream(InStream),
            SemanticName(InSemanticName),
            SemanticIndex(InSemanticIndex),
            StartComponent(0),
            ComponentCount(InComponentCount),
            OutputSlot(InOutputSlot)
        {}
    };

    //#todo-RemoveStreamOut
    typedef std::vector<FStreamOutElement> StreamOutElementList;

    struct SamplerStateInitializerRHI
    {
        SamplerStateInitializerRHI() {}
        SamplerStateInitializerRHI(
            ESamplerFilter InFilter,
            ESamplerAddressMode InAddressU = AM_Wrap,
            ESamplerAddressMode InAddressV = AM_Wrap,
            ESamplerAddressMode InAddressW = AM_Wrap,
            float InMipBias = 0,
            int32 InMaxAnisotropy = 0,
            float InMinMipLevel = 0,
            float InMaxMipLevel = FLT_MAX,
            uint32_t InBorderColor = 0,
            /** Only supported in D3D11 */
            ESamplerCompareFunction InSamplerComparisonFunction = SCF_Never
            )
        :	Filter(InFilter)
        ,	AddressU(InAddressU)
        ,	AddressV(InAddressV)
        ,	AddressW(InAddressW)
        ,	MipBias(InMipBias)
        ,	MinMipLevel(InMinMipLevel)
        ,	MaxMipLevel(InMaxMipLevel)
        ,	MaxAnisotropy(InMaxAnisotropy)
        ,	BorderColor(InBorderColor)
        ,	SamplerComparisonFunction(InSamplerComparisonFunction)
        {
        }
        ESamplerFilter Filter = SF_Point;
        ESamplerAddressMode AddressU = AM_Wrap;
        ESamplerAddressMode AddressV = AM_Wrap;
        ESamplerAddressMode AddressW = AM_Wrap;
        float MipBias = 0.0f;
        /** Smallest mip map level that will be used, where 0 is the highest resolution mip level. */
        float MinMipLevel = 0.0f;
        /** Largest mip map level that will be used, where 0 is the highest resolution mip level. */
        float MaxMipLevel = FLT_MAX;
        int32 MaxAnisotropy = 0;
        uint32_t BorderColor = 0;
        ESamplerCompareFunction SamplerComparisonFunction = SCF_Never;


        friend uint32_t GetTypeHash(const SamplerStateInitializerRHI& Initializer);
        friend bool operator== (const SamplerStateInitializerRHI& A, const SamplerStateInitializerRHI& B);
    };

    struct RasterizerStateInitializerRHI
    {
        ERasterizerFillMode FillMode;
        ERasterizerCullMode CullMode;
        float DepthBias;
        float SlopeScaleDepthBias;
        bool bAllowMSAA;
        bool bEnableLineAA;
        

        friend uint32_t GetTypeHash(const RasterizerStateInitializerRHI& Initializer);
        friend bool operator== (const RasterizerStateInitializerRHI& A, const RasterizerStateInitializerRHI& B);
    };

    struct DepthStencilStateInitializerRHI
    {
        bool bEnableDepthWrite;
        ECompareFunction DepthTest;

        bool bEnableFrontFaceStencil;
        ECompareFunction FrontFaceStencilTest;
        EStencilOp FrontFaceStencilFailStencilOp;
        EStencilOp FrontFaceDepthFailStencilOp;
        EStencilOp FrontFacePassStencilOp;
        bool bEnableBackFaceStencil;
        ECompareFunction BackFaceStencilTest;
        EStencilOp BackFaceStencilFailStencilOp;
        EStencilOp BackFaceDepthFailStencilOp;
        EStencilOp BackFacePassStencilOp;
        uint8_t StencilReadMask;
        uint8_t StencilWriteMask;

        DepthStencilStateInitializerRHI(
            bool bInEnableDepthWrite = true,
            ECompareFunction InDepthTest = CF_LessEqual,
            bool bInEnableFrontFaceStencil = false,
            ECompareFunction InFrontFaceStencilTest = CF_Always,
            EStencilOp InFrontFaceStencilFailStencilOp = SO_Keep,
            EStencilOp InFrontFaceDepthFailStencilOp = SO_Keep,
            EStencilOp InFrontFacePassStencilOp = SO_Keep,
            bool bInEnableBackFaceStencil = false,
            ECompareFunction InBackFaceStencilTest = CF_Always,
            EStencilOp InBackFaceStencilFailStencilOp = SO_Keep,
            EStencilOp InBackFaceDepthFailStencilOp = SO_Keep,
            EStencilOp InBackFacePassStencilOp = SO_Keep,
            uint8_t InStencilReadMask = 0xFF,
            uint8_t InStencilWriteMask = 0xFF
            )
        : bEnableDepthWrite(bInEnableDepthWrite)
        , DepthTest(InDepthTest)
        , bEnableFrontFaceStencil(bInEnableFrontFaceStencil)
        , FrontFaceStencilTest(InFrontFaceStencilTest)
        , FrontFaceStencilFailStencilOp(InFrontFaceStencilFailStencilOp)
        , FrontFaceDepthFailStencilOp(InFrontFaceDepthFailStencilOp)
        , FrontFacePassStencilOp(InFrontFacePassStencilOp)
        , bEnableBackFaceStencil(bInEnableBackFaceStencil)
        , BackFaceStencilTest(InBackFaceStencilTest)
        , BackFaceStencilFailStencilOp(InBackFaceStencilFailStencilOp)
        , BackFaceDepthFailStencilOp(InBackFaceDepthFailStencilOp)
        , BackFacePassStencilOp(InBackFacePassStencilOp)
        , StencilReadMask(InStencilReadMask)
        , StencilWriteMask(InStencilWriteMask)
        {}
        
        friend uint32_t GetTypeHash(const DepthStencilStateInitializerRHI& Initializer);
        friend bool operator== (const DepthStencilStateInitializerRHI& A, const DepthStencilStateInitializerRHI& B);
    };

    class BlendStateInitializerRHI
    {
    public:

        struct FRenderTarget
        {
            enum
            {
                NUM_STRING_FIELDS = 7
            };
            EBlendOperation ColorBlendOp;
            EBlendFactor ColorSrcBlend;
            EBlendFactor ColorDestBlend;
            EBlendOperation AlphaBlendOp;
            EBlendFactor AlphaSrcBlend;
            EBlendFactor AlphaDestBlend;
            EColorWriteMask ColorWriteMask;
            
            FRenderTarget(
                EBlendOperation InColorBlendOp = BO_Add,
                EBlendFactor InColorSrcBlend = BF_One,
                EBlendFactor InColorDestBlend = BF_Zero,
                EBlendOperation InAlphaBlendOp = BO_Add,
                EBlendFactor InAlphaSrcBlend = BF_One,
                EBlendFactor InAlphaDestBlend = BF_Zero,
                EColorWriteMask InColorWriteMask = CW_RGBA
                )
            : ColorBlendOp(InColorBlendOp)
            , ColorSrcBlend(InColorSrcBlend)
            , ColorDestBlend(InColorDestBlend)
            , AlphaBlendOp(InAlphaBlendOp)
            , AlphaSrcBlend(InAlphaSrcBlend)
            , AlphaDestBlend(InAlphaDestBlend)
            , ColorWriteMask(InColorWriteMask)
            {}
            
        };

        BlendStateInitializerRHI() {}

        BlendStateInitializerRHI(const FRenderTarget& InRenderTargetBlendState, bool bInUseAlphaToCoverage = false)
        :	bUseIndependentRenderTargetBlendStates(false)
        ,	bUseAlphaToCoverage(bInUseAlphaToCoverage)
        {
            RenderTargets[0] = InRenderTargetBlendState;
        }

        template<uint32_t NumRenderTargets>
        BlendStateInitializerRHI(const std::array<FRenderTarget,NumRenderTargets>& InRenderTargetBlendStates, bool bInUseAlphaToCoverage = false)
        :	bUseIndependentRenderTargetBlendStates(NumRenderTargets > 1)
        ,	bUseAlphaToCoverage(bInUseAlphaToCoverage)
        {
            static_assert(NumRenderTargets <= MaxSimultaneousRenderTargets, "Too many render target blend states.");

            for(uint32_t RenderTargetIndex = 0;RenderTargetIndex < NumRenderTargets;++RenderTargetIndex)
            {
                RenderTargets[RenderTargetIndex] = InRenderTargetBlendStates[RenderTargetIndex];
            }
        }

        std::array<FRenderTarget,MaxSimultaneousRenderTargets> RenderTargets;
        bool bUseIndependentRenderTargetBlendStates;
        bool bUseAlphaToCoverage;
        

        friend uint32_t GetTypeHash(const BlendStateInitializerRHI::FRenderTarget& RenderTarget);
        friend bool operator== (const BlendStateInitializerRHI::FRenderTarget& A, const BlendStateInitializerRHI::FRenderTarget& B);
        
        friend uint32_t GetTypeHash(const BlendStateInitializerRHI& Initializer);
        friend bool operator== (const BlendStateInitializerRHI& A, const BlendStateInitializerRHI& B);
    };

    class GraphicsPipelineStateInitializer
    {
    public:
        using TRenderTargetFormats		= std::array<uint8/*EPixelFormat*/, MaxSimultaneousRenderTargets>;
        using TRenderTargetFlags		= std::array<uint32/*ETextureCreateFlags*/, MaxSimultaneousRenderTargets>;

        GraphicsPipelineStateInitializer()
            : BlendState(nullptr)
            , RasterizerState(nullptr)
            , DepthStencilState(nullptr)
            , RenderTargetsEnabled(0)
            , RenderTargetFormats{0}
            , RenderTargetFlags{0}
            , DepthStencilTargetFormat(EPixelFormat::Unknow)
            , DepthStencilTargetFlag(0)
            , DepthTargetLoadAction(ERenderTargetLoadAction::ENoAction)
            , DepthTargetStoreAction(ERenderTargetStoreAction::ENoAction)
            , StencilTargetLoadAction(ERenderTargetLoadAction::ENoAction)
            , StencilTargetStoreAction(ERenderTargetStoreAction::ENoAction)
            , NumSamples(0)
            , SubpassHint(ESubpassHint::None)
            , SubpassIndex(0)
            , bDepthBounds(false)
            , bHasFragmentDensityAttachment(false)
        {
        }

        GraphicsPipelineStateInitializer(
            BoundShaderStateInput		InBoundShaderState,
            RHIBlendState*				InBlendState,
            RHIRasterizerState*		    InRasterizerState,
            RHIDepthStencilState*		InDepthStencilState,
            EPrimitiveType				InPrimitiveType,
            uint32						InRenderTargetsEnabled,
            const TRenderTargetFormats&	InRenderTargetFormats,
            const TRenderTargetFlags&	InRenderTargetFlags,
            EPixelFormat				InDepthStencilTargetFormat,
            ETextureCreateFlags			InDepthStencilTargetFlag,
            ERenderTargetLoadAction		InDepthTargetLoadAction,
            ERenderTargetStoreAction	InDepthTargetStoreAction,
            ERenderTargetLoadAction		InStencilTargetLoadAction,
            ERenderTargetStoreAction	InStencilTargetStoreAction,
            FExclusiveDepthStencil		InDepthStencilAccess,
            uint32						InNumSamples,
            ESubpassHint				InSubpassHint,
            uint8						InSubpassIndex,
            bool						bInDepthBounds)
            : BoundShaderState(InBoundShaderState)
            , BlendState(InBlendState)
            , RasterizerState(InRasterizerState)
            , DepthStencilState(InDepthStencilState)
            , PrimitiveType(InPrimitiveType)
            , RenderTargetsEnabled(InRenderTargetsEnabled)
            , RenderTargetFormats(InRenderTargetFormats)
            , RenderTargetFlags(InRenderTargetFlags)
            , DepthStencilTargetFormat(InDepthStencilTargetFormat)
            , DepthStencilTargetFlag(InDepthStencilTargetFlag)
            , DepthTargetLoadAction(InDepthTargetLoadAction)
            , DepthTargetStoreAction(InDepthTargetStoreAction)
            , StencilTargetLoadAction(InStencilTargetLoadAction)
            , StencilTargetStoreAction(InStencilTargetStoreAction)
            , DepthStencilAccess(InDepthStencilAccess)
            , NumSamples(InNumSamples)
            , SubpassHint(InSubpassHint)
            , SubpassIndex(InSubpassIndex)
            , bDepthBounds(bInDepthBounds)
        {
        }

        bool operator==(const GraphicsPipelineStateInitializer& rhs) const
        {
            if (BoundShaderState.VertexDeclarationRHI != rhs.BoundShaderState.VertexDeclarationRHI ||
                BoundShaderState.VertexShaderRHI != rhs.BoundShaderState.VertexShaderRHI ||
                BoundShaderState.PixelShaderRHI != rhs.BoundShaderState.PixelShaderRHI ||
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
                BoundShaderState.GeometryShaderRHI != rhs.BoundShaderState.GeometryShaderRHI ||
    #endif
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
                BoundShaderState.DomainShaderRHI != rhs.BoundShaderState.DomainShaderRHI ||
                BoundShaderState.HullShaderRHI != rhs.BoundShaderState.HullShaderRHI ||
    #endif
                BlendState != rhs.BlendState ||
                RasterizerState != rhs.RasterizerState ||
                DepthStencilState != rhs.DepthStencilState ||
                PrimitiveType != rhs.PrimitiveType ||
                bHasFragmentDensityAttachment != rhs.bHasFragmentDensityAttachment ||
                RenderTargetsEnabled != rhs.RenderTargetsEnabled ||
                RenderTargetFormats != rhs.RenderTargetFormats || 
                RenderTargetFlags != rhs.RenderTargetFlags || 
                DepthStencilTargetFormat != rhs.DepthStencilTargetFormat || 
                DepthStencilTargetFlag != rhs.DepthStencilTargetFlag ||
                DepthTargetLoadAction != rhs.DepthTargetLoadAction ||
                DepthTargetStoreAction != rhs.DepthTargetStoreAction ||
                StencilTargetLoadAction != rhs.StencilTargetLoadAction ||
                StencilTargetStoreAction != rhs.StencilTargetStoreAction || 
                DepthStencilAccess != rhs.DepthStencilAccess ||
                NumSamples != rhs.NumSamples ||
                SubpassHint != rhs.SubpassHint ||
                SubpassIndex != rhs.SubpassIndex)
            {
                return false;
            }

            return true;
        }

        uint32 ComputeNumValidRenderTargets() const
        {
            // Get the count of valid render targets (ignore those at the end of the array with Unknow)
            if (RenderTargetsEnabled > 0)
            {
                int32 LastValidTarget = -1;
                for (int32 i = (int32)RenderTargetsEnabled - 1; i >= 0; i--)
                {
                    if (RenderTargetFormats[i] != static_cast<uint32>(EPixelFormat::Unknow))
                    {
                        LastValidTarget = i;
                        break;
                    }
                }
                return uint32(LastValidTarget + 1);
            }
            return RenderTargetsEnabled;
        }

        BoundShaderStateInput			BoundShaderState;
        RHIBlendState*					BlendState;
        RHIRasterizerState*			    RasterizerState;
        RHIDepthStencilState*			DepthStencilState;

        EPrimitiveType					PrimitiveType;
        uint32							RenderTargetsEnabled;
        TRenderTargetFormats			RenderTargetFormats;
        TRenderTargetFlags				RenderTargetFlags;
        EPixelFormat					DepthStencilTargetFormat;
        uint32							DepthStencilTargetFlag;
        ERenderTargetLoadAction			DepthTargetLoadAction;
        ERenderTargetStoreAction		DepthTargetStoreAction;
        ERenderTargetLoadAction			StencilTargetLoadAction;
        ERenderTargetStoreAction		StencilTargetStoreAction;
        FExclusiveDepthStencil			DepthStencilAccess;
        uint16							NumSamples;
        ESubpassHint					SubpassHint;
        uint8							SubpassIndex;
        bool							bDepthBounds;
        bool							bHasFragmentDensityAttachment;
    };

    #include "rhi_resource.h"

    void init_dynamic_rhi();
    void clear_dynamic_rhi();

}