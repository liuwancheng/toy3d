#pragma once

#include "math/math.h"
#include "drivers/rhi/rhi_capabilities.h"
#include "drivers/rhi/rhi_public_definitions.h"
#include "drivers/rhi/rhi_result.h"
#include "shader/shader_binding_identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class RHIBuffer;
    class RHIBufferView;
    class RHIBindingLayout;
    class RHISampler;
    class RHIShader;
    class RHITextureView;

    struct RHISubresourceRange
    {
        RHITextureAspect aspect = RHITextureAspect::Color;
        std::uint32_t first_mip = 0;
        std::uint32_t mip_count = RHI_ALL_MIPS;
        std::uint32_t first_layer = 0;
        std::uint32_t layer_count = RHI_ALL_LAYERS;
    };

    struct RHIClearValue
    {
        enum class Type : std::uint8_t
        {
            None,
            Color,
            DepthStencil
        };

        RHIClearValue() = default;

        explicit RHIClearValue(const vec4& value) : value_type(Type::Color), color(value) {}

        RHIClearValue(float depth_value, std::uint32_t stencil_value)
            : value_type(Type::DepthStencil), depth(depth_value), stencil(stencil_value)
        {
        }

        static RHIClearValue none() { return {}; }

        static RHIClearValue color_value(const vec4& value) { return RHIClearValue(value); }

        static RHIClearValue depth_stencil_value(float depth, std::uint32_t stencil)
        {
            return RHIClearValue(depth, stencil);
        }

        Type type() const { return value_type; }

        const vec4& get_clear_color() const { return color; }

        void get_clear_depth_stencil(float& out_depth, std::uint32_t& out_stencil) const
        {
            out_depth = depth;
            out_stencil = stencil;
        }

        static const RHIClearValue None;
        static const RHIClearValue Black;
        static const RHIClearValue White;
        static const RHIClearValue DepthOne;
        static const RHIClearValue DepthZero;

        Type value_type = Type::None;
        vec4 color = vec4(0.0F);
        float depth = 1.0F;
        std::uint32_t stencil = 0;
    };

    // Temporary source-compatibility name. New code must use RHIClearValue.
    using ClearValueBinding = RHIClearValue;

    struct RHIInitialData
    {
        enum class Consumption : std::uint8_t
        {
            CopiedBeforeReturn,
            ValidUntilCommandSubmission
        };

        const void* data = nullptr;
        std::size_t size = 0;
        std::size_t row_pitch = 0;
        std::size_t slice_pitch = 0;
        Consumption consumption = Consumption::CopiedBeforeReturn;
    };

    struct RHIBufferDesc
    {
        std::uint64_t size = 0;
        std::uint32_t structure_stride = 0;
        RHIResourceUsage usage = RHIResourceUsage::None;
        // Expresses access intent only; it does not select a native heap or
        // promise persistent mapping.
        RHICPUAccess cpu_access = RHICPUAccess::None;
        RHIAccess initial_access = RHIAccess::Unknown;
        std::string debug_name;
    };

    struct RHITextureDesc
    {
        RHIResourceDimension dimension = RHIResourceDimension::Texture2D;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t depth = 1;
        std::uint32_t array_layers = 1;
        std::uint32_t mip_levels = 1;
        std::uint32_t sample_count = 1;
        PixelFormat format = PixelFormat::Unknown;
        RHIResourceUsage usage = RHIResourceUsage::None;
        // Expresses access intent only; it does not select a native heap or
        // promise persistent mapping.
        RHICPUAccess cpu_access = RHICPUAccess::None;
        RHIAccess initial_access = RHIAccess::Unknown;
        RHIClearValue clear_value;
        std::string debug_name;
    };

    struct RHITextureViewDesc
    {
        RHIResourceViewType type = RHIResourceViewType::ShaderResource;
        RHITextureViewDimension dimension = RHITextureViewDimension::Texture2D;
        PixelFormat format = PixelFormat::Unknown;
        RHISubresourceRange subresources;
        bool depth_read_only = false;
        bool stencil_read_only = false;
        std::string debug_name;
    };

    struct RHIBufferViewDesc
    {
        RHIResourceViewType type = RHIResourceViewType::ShaderResource;
        PixelFormat format = PixelFormat::Unknown;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::string debug_name;
    };

    struct RHIShaderBytecode
    {
        std::vector<std::uint8_t> bytes;
        std::string target;
    };

    struct RHIShaderBindingReflection
    {
        ShaderParameterId binding_id = 0;
        std::string name;
        RHIBindingGroup group = RHIBindingGroup::Material;
        std::uint32_t target_binding = 0;
        RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
        std::uint32_t array_count = 1;
        std::uint32_t data_size = 0;
        ShaderDataLayoutHash data_layout_hash{};
        std::uint32_t shader_abi_version = 0;
    };

    struct RHIShaderVertexInputReflection
    {
        enum class ScalarType
        {
            Float32,
            Int32,
            UInt32
        };

        std::string semantic_name;
        std::uint32_t semantic_index = 0;
        std::uint32_t location = 0;
        ScalarType scalar_type = ScalarType::Float32;
        std::uint32_t component_count = 0;

        bool operator==(const RHIShaderVertexInputReflection& other) const;
    };

    struct RHIShaderDesc
    {
        RHIShaderStage stage = RHIShaderStage::Vertex;
        RHIShaderBytecode bytecode;
        std::string entry_point = "main";
        std::vector<RHIShaderBindingReflection> reflection;
        std::vector<RHIShaderVertexInputReflection> vertex_inputs;
        std::array<std::uint64_t, 2> content_hash = {0, 0};
        std::string debug_name;
    };

    struct RHIBindingLayoutEntry
    {
        ShaderParameterId binding_id = 0;
        RHIBindingGroup group = RHIBindingGroup::Material;
        std::uint32_t target_binding = 0;
        RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
        RHIShaderStageFlags stages = RHIShaderStageFlags::None;
        std::uint32_t array_count = 1;
        std::uint32_t data_size = 0;
        ShaderDataLayoutHash data_layout_hash{};
        std::uint32_t shader_abi_version = 0;

        bool operator==(const RHIBindingLayoutEntry& other) const;
    };

    struct RHIBindingLayoutDesc
    {
        std::vector<RHIBindingLayoutEntry> entries;
        std::string debug_name;

        bool operator==(const RHIBindingLayoutDesc& other) const;
    };

    struct RHISamplerDesc
    {
        RHIFilter min_filter = RHIFilter::Linear;
        RHIFilter mag_filter = RHIFilter::Linear;
        RHIFilter mip_filter = RHIFilter::Linear;
        RHIAddressMode address_u = RHIAddressMode::Repeat;
        RHIAddressMode address_v = RHIAddressMode::Repeat;
        RHIAddressMode address_w = RHIAddressMode::Repeat;
        float mip_lod_bias = 0.0F;
        std::uint32_t max_anisotropy = 1;
        bool compare_enable = false;
        RHICompareOperation compare_operation = RHICompareOperation::Always;
        float min_lod = 0.0F;
        float max_lod = 1000.0F;
        RHIBorderColor border_color = RHIBorderColor::TransparentBlack;
        std::string debug_name;
    };

    struct RHIBindingValue
    {
        ShaderParameterId binding_id = 0;
        std::uint32_t array_index = 0;
        std::shared_ptr<RHIBuffer> buffer;
        std::shared_ptr<RHIBufferView> buffer_view;
        std::shared_ptr<RHITextureView> texture_view;
        std::shared_ptr<RHISampler> sampler;
        std::uint64_t buffer_offset = 0;
        std::uint64_t buffer_size = 0;
        ShaderDataLayoutHash data_layout_hash{};
        std::uint32_t shader_abi_version = 0;
    };

    struct RHIBindingSetDesc
    {
        RHIBindingGroup group = RHIBindingGroup::Material;
        std::vector<RHIBindingValue> bindings;
        std::string debug_name;
    };

    struct RHIGraphicsPipelineDesc
    {
        std::shared_ptr<RHIShader> vertex_shader;
        std::shared_ptr<RHIShader> pixel_shader;
        std::shared_ptr<RHIBindingLayout> binding_layout;
        RHIPrimitiveTopology primitive_topology = RHIPrimitiveTopology::TriangleList;
        struct VertexBufferLayout
        {
            std::uint32_t binding = 0;
            std::uint32_t stride = 0;
            RHIVertexInputRate input_rate = RHIVertexInputRate::PerVertex;
        };

        struct VertexAttribute
        {
            // Location links this layout element to vertex-shader reflection.
            // Vulkan consumes it directly; D3D backends use the linked
            // reflection semantic name/index when creating their input layout.
            std::uint32_t location = 0;
            std::uint32_t binding = 0;
            PixelFormat format = PixelFormat::Unknown;
            std::uint32_t offset = 0;
        };

        struct RasterizationState
        {
            RHIPolygonMode polygon_mode = RHIPolygonMode::Fill;
            RHICullMode cull_mode = RHICullMode::Back;
            RHIFrontFace front_face = RHIFrontFace::CounterClockwise;
            bool depth_clamp_enable = false;
        };

        struct StencilFaceState
        {
            RHIStencilOperation fail_operation = RHIStencilOperation::Keep;
            RHIStencilOperation depth_fail_operation = RHIStencilOperation::Keep;
            RHIStencilOperation pass_operation = RHIStencilOperation::Keep;
            RHICompareOperation compare_operation = RHICompareOperation::Always;
        };

        struct DepthStencilState
        {
            bool depth_test_enable = false;
            bool depth_write_enable = false;
            RHICompareOperation depth_compare_operation = RHICompareOperation::LessEqual;
            bool stencil_test_enable = false;
            // D3D10 and the baseline D3D12 depth-stencil state expose one pair
            // of masks for both faces. Stencil reference is dynamic command
            // state and therefore does not belong to the pipeline descriptor.
            std::uint8_t stencil_read_mask = 0xffU;
            std::uint8_t stencil_write_mask = 0xffU;
            StencilFaceState front_face;
            StencilFaceState back_face;
        };

        struct ColorBlendAttachmentState
        {
            bool blend_enable = false;
            RHIBlendFactor source_color_factor = RHIBlendFactor::One;
            RHIBlendFactor destination_color_factor = RHIBlendFactor::Zero;
            RHIBlendOperation color_operation = RHIBlendOperation::Add;
            RHIBlendFactor source_alpha_factor = RHIBlendFactor::One;
            RHIBlendFactor destination_alpha_factor = RHIBlendFactor::Zero;
            RHIBlendOperation alpha_operation = RHIBlendOperation::Add;
            RHIColorWriteMask color_write_mask = RHIColorWriteMask::All;
        };

        std::vector<VertexBufferLayout> vertex_buffers;
        std::vector<VertexAttribute> vertex_attributes;
        RasterizationState rasterization;
        DepthStencilState depth_stencil;
        std::array<PixelFormat, RHI_MAX_COLOR_ATTACHMENTS> color_formats = {};
        std::array<ColorBlendAttachmentState, RHI_MAX_COLOR_ATTACHMENTS> color_blend_attachments = {};
        std::uint32_t color_attachment_count = 0;
        PixelFormat depth_stencil_format = PixelFormat::Unknown;
        std::uint32_t sample_count = 1;
        std::string debug_name;
    };

    RHIStatus validate_buffer_desc(const RHIBufferDesc& desc);
    RHIStatus validate_buffer_initial_data(const RHIBufferDesc& desc, const RHIInitialData& initial_data);
    RHIStatus validate_texture_desc(const RHITextureDesc& desc);
    RHIStatus validate_texture_format_capabilities(const RHITextureDesc& desc,
                                                   const RHIFormatCapabilities& capabilities);
    RHIStatus validate_texture_subresource_range(const RHITextureDesc& texture_desc, const RHISubresourceRange& range);
    RHIStatus validate_texture_initial_data(const RHITextureDesc& desc, const RHIInitialData& initial_data);
    RHIStatus validate_texture_view_desc(const RHITextureDesc& texture_desc, const RHITextureViewDesc& view_desc);
    RHIStatus validate_buffer_view_desc(const RHIBufferDesc& buffer_desc, const RHIBufferViewDesc& view_desc);
    RHIStatus validate_shader_desc(const RHIShaderDesc& desc);
    RHIStatus validate_binding_layout_desc(const RHIBindingLayoutDesc& desc);
    RHIStatus validate_sampler_desc(const RHISamplerDesc& desc);
    RHIStatus validate_binding_set_desc(const RHIBindingSetDesc& desc);
    RHIStatus validate_graphics_pipeline_desc(const RHIGraphicsPipelineDesc& desc);
} // namespace toy3d
