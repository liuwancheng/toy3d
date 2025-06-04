#pragma once
#include "core/misc/pch.h"
#include "core/math/math.h"
#include "drivers/rhi/rhi_definitions.h"

namespace toy3d
{
    class RHIBlendState;
    class RHIRasterizerState;
    class RHIDepthStencilState;
    class RHIVertexDeclaration;
    class RHIVertexShader;
    class RHIHullShader;
    class RHIDomainShader;
    class RHIPixelShader;
    class RHIGeometryShader;
    class RHITexture;

    /** Info for supporting the vertex element types */
    struct VertexElementTypeSupportInfo
    {
        /** cap bit set for each VET. One-to-one mapping based on EVertexElementType */
        bool element_caps[VET_Max];
        
        VertexElementTypeSupportInfo() { for(int32 i=0; i<VET_Max; i++) element_caps[i]=true; }
        bool is_supported(EVertexElementType element_type) { return element_caps[element_type]; }
        void set_supported(EVertexElementType element_type, bool is_supported) { element_caps[element_type]=is_supported; }
    };

    struct VertexElement
    {
        uint8_t stream_index;
        uint8_t offset;
        EVertexElementType type;
        uint8_t attribute_index;
        uint16 stride;
        /**
         * Whether to use instance index or vertex index to consume the element.  
         * eg if use_instance_index is 0, the element will be repeated for every instance.
         */
        uint16 use_instance_index;

        VertexElement() {}
        VertexElement(uint8_t in_stream_index, uint8_t in_offset, EVertexElementType in_type, uint8_t in_attribute_index, uint16 in_stride, bool in_use_instance_index = false):
            stream_index(in_stream_index),
            offset(in_offset),
            type(in_type),
            attribute_index(in_attribute_index),
            stride(in_stride),
            use_instance_index(in_use_instance_index)
        {}
        /**
        * Suppress the compiler generated assignment operator so that padding won't be copied.
        * This is necessary to get expected results for code that zeros, assigns and then CRC's the whole struct.
        */
        void operator=(const VertexElement& other)
        {
            stream_index = other.stream_index;
            offset = other.offset;
            type = other.type;
            attribute_index = other.attribute_index;
            stride = other.stride;
            use_instance_index = other.use_instance_index;
        }
    };

    typedef std::vector<VertexElement> VertexDeclarationElementList;

    /** RHI representation of a single stream out element. */
    //#todo-RemoveStreamOut
    struct StreamOutElement
    {
        /** Index of the output stream from the geometry shader. */
        uint32_t stream;

        /** Semantic name of the output element as defined in the geometry shader. This should not contain the semantic number. */
        const char* semantic_name;

        /** Semantic index of the output element as defined in the geometry shader. For example "TEXCOORD5" in the shader would give a semantic_index of 5. */
        uint32_t semantic_index;

        /** Start component index of the shader output element to stream out. */
        uint8_t start_component;

        /** Number of components of the shader output element to stream out. */
        uint8_t component_count;

        /** Stream output target slot, corresponding to the streams set by RHISetStreamOutTargets. */
        uint8_t output_slot;

        StreamOutElement() {}
        StreamOutElement(uint32_t in_stream, const char* in_semantic_name, uint32_t in_semantic_index, uint8_t in_component_count, uint8_t in_output_slot) :
            stream(in_stream),
            semantic_name(in_semantic_name),
            semantic_index(in_semantic_index),
            start_component(0),
            component_count(in_component_count),
            output_slot(in_output_slot)
        {}
    };

    //#todo-RemoveStreamOut
    typedef std::vector<StreamOutElement> StreamOutElementList;

    struct SamplerStateInitializerRHI
    {
        SamplerStateInitializerRHI() {}
        SamplerStateInitializerRHI(
            ESamplerFilter in_filter,
            ESamplerAddressMode in_address_u = AM_Wrap,
            ESamplerAddressMode in_address_v = AM_Wrap,
            ESamplerAddressMode in_address_w = AM_Wrap,
            float in_mip_bias = 0,
            int32 in_max_anisotropy = 0,
            float in_min_mip_level = 0,
            float in_max_mip_level = FLT_MAX,
            uint32_t in_border_color = 0,
            /** Only supported in D3D11 */
            ESamplerCompareFunction in_sampler_comparison_function = SCF_Never
            )
        :	filter(in_filter)
        ,	address_u(in_address_u)
        ,	address_v(in_address_v)
        ,	address_w(in_address_w)
        ,	mip_bias(in_mip_bias)
        ,	min_mip_level(in_min_mip_level)
        ,	max_mip_level(in_max_mip_level)
        ,	max_anisotropy(in_max_anisotropy)
        ,	border_color(in_border_color)
        ,	sampler_comparison_function(in_sampler_comparison_function)
        {
        }
        ESamplerFilter filter = SF_Point;
        ESamplerAddressMode address_u = AM_Wrap;
        ESamplerAddressMode address_v = AM_Wrap;
        ESamplerAddressMode address_w = AM_Wrap;
        float mip_bias = 0.0f;
        /** Smallest mip map level that will be used, where 0 is the highest resolution mip level. */
        float min_mip_level = 0.0f;
        /** Largest mip map level that will be used, where 0 is the highest resolution mip level. */
        float max_mip_level = FLT_MAX;
        int32 max_anisotropy = 0;
        uint32_t border_color = 0;
        ESamplerCompareFunction sampler_comparison_function = SCF_Never;


        friend uint32_t get_type_hash(const SamplerStateInitializerRHI& initializer);
        friend bool operator== (const SamplerStateInitializerRHI& a, const SamplerStateInitializerRHI& b);
    };

    struct RasterizerStateInitializerRHI
    {
        ERasterizerFillMode fill_mode;
        ERasterizerCullMode cull_mode;
        float depth_bias;
        float slope_scale_depth_bias;
        bool allow_msaa;
        bool enable_line_aa;
        

        friend uint32_t get_type_hash(const RasterizerStateInitializerRHI& initializer);
        friend bool operator== (const RasterizerStateInitializerRHI& a, const RasterizerStateInitializerRHI& b);
    };

    struct DepthStencilStateInitializerRHI
    {
        bool enable_depth_write;
        ECompareFunction depth_test;

        bool enable_front_face_stencil;
        ECompareFunction front_face_stencil_test;
        EStencilOp front_face_stencil_fail_stencil_op;
        EStencilOp front_face_depth_fail_stencil_op;
        EStencilOp front_face_pass_stencil_op;
        bool enable_back_face_stencil;
        ECompareFunction back_face_stencil_test;
        EStencilOp back_face_stencil_fail_stencil_op;
        EStencilOp back_face_depth_fail_stencil_op;
        EStencilOp back_face_pass_stencil_op;
        uint8_t stencil_read_mask;
        uint8_t stencil_write_mask;

        DepthStencilStateInitializerRHI(
            bool in_enable_depth_write = true,
            ECompareFunction in_depth_test = CF_LessEqual,
            bool in_enable_front_face_stencil = false,
            ECompareFunction in_front_face_stencil_test = CF_Always,
            EStencilOp in_front_face_stencil_fail_stencil_op = SO_Keep,
            EStencilOp in_front_face_depth_fail_stencil_op = SO_Keep,
            EStencilOp in_front_face_pass_stencil_op = SO_Keep,
            bool in_enable_back_face_stencil = false,
            ECompareFunction in_back_face_stencil_test = CF_Always,
            EStencilOp in_back_face_stencil_fail_stencil_op = SO_Keep,
            EStencilOp in_back_face_depth_fail_stencil_op = SO_Keep,
            EStencilOp in_back_face_pass_stencil_op = SO_Keep,
            uint8_t in_stencil_read_mask = 0xFF,
            uint8_t in_stencil_write_mask = 0xFF
            )
        : enable_depth_write(in_enable_depth_write)
        , depth_test(in_depth_test)
        , enable_front_face_stencil(in_enable_front_face_stencil)
        , front_face_stencil_test(in_front_face_stencil_test)
        , front_face_stencil_fail_stencil_op(in_front_face_stencil_fail_stencil_op)
        , front_face_depth_fail_stencil_op(in_front_face_depth_fail_stencil_op)
        , front_face_pass_stencil_op(in_front_face_pass_stencil_op)
        , enable_back_face_stencil(in_enable_back_face_stencil)
        , back_face_stencil_test(in_back_face_stencil_test)
        , back_face_stencil_fail_stencil_op(in_back_face_stencil_fail_stencil_op)
        , back_face_depth_fail_stencil_op(in_back_face_depth_fail_stencil_op)
        , back_face_pass_stencil_op(in_back_face_pass_stencil_op)
        , stencil_read_mask(in_stencil_read_mask)
        , stencil_write_mask(in_stencil_write_mask)
        {}
        
        friend uint32_t get_type_hash(const DepthStencilStateInitializerRHI& initializer);
        friend bool operator== (const DepthStencilStateInitializerRHI& a, const DepthStencilStateInitializerRHI& b);
    };

    struct BlendStateInitializerRHI
    {
        struct PerRenderTargetBlendState
        {
            enum
            {
                NUM_STRING_FIELDS = 7
            };
            EBlendOperation color_blend_op;
            EBlendFactor color_src_blend;
            EBlendFactor color_dest_blend;
            EBlendOperation alpha_blend_op;
            EBlendFactor alpha_src_blend;
            EBlendFactor alpha_dest_blend;
            EColorWriteMask color_write_mask;
            
            PerRenderTargetBlendState(
                EBlendOperation in_color_blend_op = BO_Add,
                EBlendFactor in_color_src_blend = BF_One,
                EBlendFactor in_color_dest_blend = BF_Zero,
                EBlendOperation in_alpha_blend_op = BO_Add,
                EBlendFactor in_alpha_src_blend = BF_One,
                EBlendFactor in_alpha_dest_blend = BF_Zero,
                EColorWriteMask in_color_write_mask = CW_RGBA
                )
            : color_blend_op(in_color_blend_op)
            , color_src_blend(in_color_src_blend)
            , color_dest_blend(in_color_dest_blend)
            , alpha_blend_op(in_alpha_blend_op)
            , alpha_src_blend(in_alpha_src_blend)
            , alpha_dest_blend(in_alpha_dest_blend)
            , color_write_mask(in_color_write_mask)
            {}
            
        };

        BlendStateInitializerRHI() {}

        BlendStateInitializerRHI(const PerRenderTargetBlendState& in_render_target_blend_state, bool in_use_alpha_to_coverage = false)
        :	use_independent_render_target_blend_states(false)
        ,	use_alpha_to_coverage(in_use_alpha_to_coverage)
        {
            render_targets[0] = in_render_target_blend_state;
        }

        template<uint32_t NumRenderTargets>
        BlendStateInitializerRHI(const std::array<PerRenderTargetBlendState,NumRenderTargets>& in_render_target_blend_states, bool in_use_alpha_to_coverage = false)
        :	use_independent_render_target_blend_states(NumRenderTargets > 1)
        ,	use_alpha_to_coverage(in_use_alpha_to_coverage)
        {
            static_assert(NumRenderTargets <= MaxSimultaneousRenderTargets, "Too many render target blend states.");

            for(uint32_t render_target_index = 0; render_target_index < NumRenderTargets; ++render_target_index)
            {
                render_targets[render_target_index] = in_render_target_blend_states[render_target_index];
            }
        }

        std::array<PerRenderTargetBlendState,MaxSimultaneousRenderTargets> render_targets;
        bool use_independent_render_target_blend_states;
        bool use_alpha_to_coverage;
        

        friend uint32_t get_type_hash(const BlendStateInitializerRHI::PerRenderTargetBlendState& render_target);
        friend bool operator== (const BlendStateInitializerRHI::PerRenderTargetBlendState& a, const BlendStateInitializerRHI::PerRenderTargetBlendState& b);
        
        friend uint32_t get_type_hash(const BlendStateInitializerRHI& initializer);
        friend bool operator== (const BlendStateInitializerRHI& a, const BlendStateInitializerRHI& b);
    };

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

    struct GraphicsPipelineStateInitializerRHI
    {
        using TRenderTargetFormats = std::array<uint8/*EPixelFormat*/, MaxSimultaneousRenderTargets>;
        using TRenderTargetFlags = std::array<uint32/*ETextureCreateFlags*/, MaxSimultaneousRenderTargets>;

        GraphicsPipelineStateInitializerRHI()
            : blend_state(nullptr)
            , rasterizer_state(nullptr)
            , depth_stencil_state(nullptr)
            , render_targets_enabled(0)
            , render_target_formats{0}
            , render_target_flags{0}
            , depth_stencil_target_format(EPixelFormat::Unknow)
            , depth_stencil_target_flag(0)
            , depth_target_load_action(ERenderTargetLoadAction::ENoAction)
            , depth_target_store_action(ERenderTargetStoreAction::ENoAction)
            , stencil_target_load_action(ERenderTargetLoadAction::ENoAction)
            , stencil_target_store_action(ERenderTargetStoreAction::ENoAction)
            , num_samples(0)
            , subpass_hint(ESubpassHint::None)
            , subpass_index(0)
            , depth_bounds(false)
            , has_fragment_density_attachment(false)
        {
        }

        GraphicsPipelineStateInitializerRHI(
            BoundShaderStateInput           in_bound_shader_state,
            RHIBlendState*                  in_blend_state,
            RHIRasterizerState*             in_rasterizer_state,
            RHIDepthStencilState*           in_depth_stencil_state,
            EPrimitiveType                  in_primitive_type,
            uint32                          in_render_targets_enabled,
            const TRenderTargetFormats&  in_render_target_formats,
            const TRenderTargetFlags&    in_render_target_flags,
            EPixelFormat                    in_depth_stencil_target_format,
            ETextureCreateFlags             in_depth_stencil_target_flag,
            ERenderTargetLoadAction         in_depth_target_load_action,
            ERenderTargetStoreAction        in_depth_target_store_action,
            ERenderTargetLoadAction         in_stencil_target_load_action,
            ERenderTargetStoreAction        in_stencil_target_store_action,
            ExclusiveDepthStencil           in_depth_stencil_access,
            uint32                          in_num_samples,
            ESubpassHint                    in_subpass_hint,
            uint8                           in_subpass_index,
            bool                            in_depth_bounds)
            : bound_shader_state(in_bound_shader_state)
            , blend_state(in_blend_state)
            , rasterizer_state(in_rasterizer_state)
            , depth_stencil_state(in_depth_stencil_state)
            , primitive_type(in_primitive_type)
            , render_targets_enabled(in_render_targets_enabled)
            , render_target_formats(in_render_target_formats)
            , render_target_flags(in_render_target_flags)
            , depth_stencil_target_format(in_depth_stencil_target_format)
            , depth_stencil_target_flag(in_depth_stencil_target_flag)
            , depth_target_load_action(in_depth_target_load_action)
            , depth_target_store_action(in_depth_target_store_action)
            , stencil_target_load_action(in_stencil_target_load_action)
            , stencil_target_store_action(in_stencil_target_store_action)
            , depth_stencil_access(in_depth_stencil_access)
            , num_samples(in_num_samples)
            , subpass_hint(in_subpass_hint)
            , subpass_index(in_subpass_index)
            , depth_bounds(in_depth_bounds)
        {
        }

        bool operator==(const GraphicsPipelineStateInitializerRHI& rhs) const
        {
            if (bound_shader_state.vertex_declaration_rhi != rhs.bound_shader_state.vertex_declaration_rhi ||
                bound_shader_state.vertex_shader_rhi != rhs.bound_shader_state.vertex_shader_rhi ||
                bound_shader_state.pixel_shader_rhi != rhs.bound_shader_state.pixel_shader_rhi ||
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
                bound_shader_state.geometry_shader_rhi != rhs.bound_shader_state.geometry_shader_rhi ||
    #endif
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
                bound_shader_state.domain_shader_rhi != rhs.bound_shader_state.domain_shader_rhi ||
                bound_shader_state.hull_shader_rhi != rhs.bound_shader_state.hull_shader_rhi ||
    #endif
                blend_state != rhs.blend_state ||
                rasterizer_state != rhs.rasterizer_state ||
                depth_stencil_state != rhs.depth_stencil_state ||
                primitive_type != rhs.primitive_type ||
                has_fragment_density_attachment != rhs.has_fragment_density_attachment ||
                render_targets_enabled != rhs.render_targets_enabled ||
                render_target_formats != rhs.render_target_formats || 
                render_target_flags != rhs.render_target_flags || 
                depth_stencil_target_format != rhs.depth_stencil_target_format || 
                depth_stencil_target_flag != rhs.depth_stencil_target_flag ||
                depth_target_load_action != rhs.depth_target_load_action ||
                depth_target_store_action != rhs.depth_target_store_action ||
                stencil_target_load_action != rhs.stencil_target_load_action ||
                stencil_target_store_action != rhs.stencil_target_store_action || 
                depth_stencil_access != rhs.depth_stencil_access ||
                num_samples != rhs.num_samples ||
                subpass_hint != rhs.subpass_hint ||
                subpass_index != rhs.subpass_index)
            {
                return false;
            }

            return true;
        }

        uint32 compute_num_valid_render_targets() const
        {
            // Get the count of valid render targets (ignore those at the end of the array with Unknow)
            if (render_targets_enabled > 0)
            {
                int32 last_valid_target = -1;
                for (int32 i = (int32)render_targets_enabled - 1; i >= 0; i--)
                {
                    if (render_target_formats[i] != static_cast<uint32>(EPixelFormat::Unknow))
                    {
                        last_valid_target = i;
                        break;
                    }
                }
                return uint32(last_valid_target + 1);
            }
            return render_targets_enabled;
        }

        BoundShaderStateInput           bound_shader_state;
        RHIBlendState*                  blend_state;
        RHIRasterizerState*             rasterizer_state;
        RHIDepthStencilState*           depth_stencil_state;

        EPrimitiveType                  primitive_type;
        uint32                          render_targets_enabled;
        TRenderTargetFormats            render_target_formats;
        TRenderTargetFlags              render_target_flags;
        EPixelFormat                    depth_stencil_target_format;
        uint32                          depth_stencil_target_flag;
        ERenderTargetLoadAction         depth_target_load_action;
        ERenderTargetStoreAction        depth_target_store_action;
        ERenderTargetLoadAction         stencil_target_load_action;
        ERenderTargetStoreAction        stencil_target_store_action;
        ExclusiveDepthStencil           depth_stencil_access;
        uint16                          num_samples;
        ESubpassHint                    subpass_hint;
        uint8                           subpass_index;
        bool                            depth_bounds;
        bool                            has_fragment_density_attachment;
    };

    struct ClearValueBinding
    {
        struct DSValue
        {
            float depth;
            uint32 stencil;
        }

        union ClearValueTye
        {
            float color[4];
            DSValue depth_stencil;
        }Value;

        enum EClearBinding
        {
            ENoneBound,
            EColorBound,
            EDepthStencilBound
        }

        EClearBinding binding_flag;

        ClearValueBinding(const vec4 & in_color)
        {
            Value.color[0] = in_color.x;
            Value.color[1] = in_color.y;
            Value.color[2] = in_color.z;
            Value.color[3] = in_color.w;
            binding_flag = EColorBound;
        }

        ClearValueBinding(float in_depth, uint32 in_stencil)
        {
            Value.depth_stencil.depth = in_depth;
            Value.depth_stencil.stencil = in_stencil;
            binding_flag = EDepthStencilBound;
        }

        ClearValueBinding()
        {
            Value.color[0] = 0.f;
            Value.color[1] = 0.f;
            Value.color[2] = 0.f;
            Value.color[3] = 0.f;
            binding_flag = ENoneBound;
        }

        vec4 get_clear_color() const 
        {
            return vec4(Value.color[0], Value.color[1], Value.color[2], Value.color[3]);
        }

        void get_clear_depth_stencil(float& out_depth, uint32& out_stencil) const
        {
            out_depth = Value.depth_stencil.depth;
            out_stencil = Value.depth_stencil.stencil;
        }

        static const ClearValueBinding None;
        static const ClearValueBinding Black;
        static const ClearValueBinding White;
        static const ClearValueBinding DepthOne;
        static const ClearValueBinding DepthZero;
        static const ClearValueBinding DepthNear;
        static const ClearValueBinding DepthFar;
    };

    const ClearValueBinding ClearValueBinding::None = ClearValueBinding();
    const ClearValueBinding ClearValueBinding::Black = ClearValueBinding(0.0f, 0.0f, 0.0f, 0.0f);
    const ClearValueBinding ClearValueBinding::White = ClearValueBinding(1.0f, 1.0f, 1.0f, 1.0f);
    const ClearValueBinding ClearValueBinding::DepthZero = ClearValueBinding(0.0f, 0);
    const ClearValueBinding ClearValueBinding::DepthOne = ClearValueBinding(1.0f, 0);
    const ClearValueBinding ClearValueBinding::DepthNear = ClearValueBinding(1.0f, 0);  
    const ClearValueBinding ClearValueBinding::DepthFar = ClearValueBinding(0.0f, 0);
}

