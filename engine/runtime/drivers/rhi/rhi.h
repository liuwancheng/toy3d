#pragma once
#include "core/misc/pch.h"
#include "core/math/math.h"
#include "rhi_definitions.h"

namespace toy3d
{

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
            if (bound_shader_state.VertexDeclarationRHI != rhs.bound_shader_state.VertexDeclarationRHI ||
                bound_shader_state.VertexShaderRHI != rhs.bound_shader_state.VertexShaderRHI ||
                bound_shader_state.PixelShaderRHI != rhs.bound_shader_state.PixelShaderRHI ||
    #if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
                bound_shader_state.GeometryShaderRHI != rhs.bound_shader_state.GeometryShaderRHI ||
    #endif
    #if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
                bound_shader_state.DomainShaderRHI != rhs.bound_shader_state.DomainShaderRHI ||
                bound_shader_state.HullShaderRHI != rhs.bound_shader_state.HullShaderRHI ||
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
        TRenderTargetFormats         render_target_formats;
        TRenderTargetFlags           render_target_flags;
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

    #include "rhi_resource.h"

    void init_dynamic_rhi();
    void clear_dynamic_rhi();

}
