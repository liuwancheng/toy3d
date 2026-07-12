#pragma once

#include "core/math/math.h"
#include "drivers/rhi/rhi_public_definitions.h"
#include "drivers/rhi/rhi_result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class RHIBindingLayout;
    class RHIShader;

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

        explicit RHIClearValue(const vec4& value)
            : value_type(Type::Color)
            , color(value)
        {
        }

        RHIClearValue(float depth_value, std::uint32_t stencil_value)
            : value_type(Type::DepthStencil)
            , depth(depth_value)
            , stencil(stencil_value)
        {
        }

        static RHIClearValue none()
        {
            return {};
        }

        static RHIClearValue color_value(const vec4& value)
        {
            return RHIClearValue(value);
        }

        static RHIClearValue depth_stencil_value(float depth, std::uint32_t stencil)
        {
            return RHIClearValue(depth, stencil);
        }

        Type type() const
        {
            return value_type;
        }

        const vec4& get_clear_color() const
        {
            return color;
        }

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
        std::uint32_t stride = 0;
        RHIResourceUsage usage = RHIResourceUsage::None;
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
        RHIFormat format = RHIFormat::Unknown;
        RHIResourceUsage usage = RHIResourceUsage::None;
        RHICPUAccess cpu_access = RHICPUAccess::None;
        RHIAccess initial_access = RHIAccess::Unknown;
        RHIClearValue clear_value;
        std::string debug_name;
    };

    struct RHITextureViewDesc
    {
        RHIResourceViewType type = RHIResourceViewType::ShaderResource;
        RHITextureViewDimension dimension = RHITextureViewDimension::Texture2D;
        RHIFormat format = RHIFormat::Unknown;
        RHISubresourceRange subresources;
        bool depth_read_only = false;
        bool stencil_read_only = false;
        std::string debug_name;
    };

    struct RHIBufferViewDesc
    {
        RHIResourceViewType type = RHIResourceViewType::ShaderResource;
        RHIFormat format = RHIFormat::Unknown;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::uint32_t stride = 0;
        std::string debug_name;
    };

    struct RHIShaderBytecode
    {
        std::vector<std::uint8_t> bytes;
        std::string target;
    };

    struct RHIShaderBindingReflection
    {
        std::string name;
        RHIBindingGroup group = RHIBindingGroup::Material;
        std::uint32_t slot = 0;
        RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
        std::uint32_t array_count = 1;
    };

    struct RHIShaderDesc
    {
        RHIShaderStage stage = RHIShaderStage::Vertex;
        RHIShaderBytecode bytecode;
        std::string entry_point = "main";
        std::vector<RHIShaderBindingReflection> reflection;
        std::array<std::uint64_t, 2> content_hash = {0, 0};
        std::string debug_name;
    };

    struct RHIBindingLayoutEntry
    {
        RHIBindingGroup group = RHIBindingGroup::Material;
        std::uint32_t slot = 0;
        RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
        RHIShaderStageFlags stages = RHIShaderStageFlags::None;
        std::uint32_t array_count = 1;

        bool operator==(const RHIBindingLayoutEntry& other) const;
    };

    struct RHIBindingLayoutDesc
    {
        std::vector<RHIBindingLayoutEntry> entries;
        std::string debug_name;

        bool operator==(const RHIBindingLayoutDesc& other) const;
    };

    struct RHIGraphicsPipelineDesc
    {
        std::shared_ptr<RHIShader> vertex_shader;
        std::shared_ptr<RHIShader> pixel_shader;
        std::shared_ptr<RHIBindingLayout> binding_layout;
        RHIPrimitiveTopology primitive_topology = RHIPrimitiveTopology::TriangleList;
        std::array<RHIFormat, 8> color_formats = {};
        std::uint32_t color_attachment_count = 0;
        RHIFormat depth_stencil_format = RHIFormat::Unknown;
        std::uint32_t sample_count = 1;
        std::string debug_name;
    };

    RHIStatus validate_buffer_desc(const RHIBufferDesc& desc);
    RHIStatus validate_buffer_initial_data(
        const RHIBufferDesc& desc,
        const RHIInitialData& initial_data);
    RHIStatus validate_texture_desc(const RHITextureDesc& desc);
    RHIStatus validate_texture_initial_data(
        const RHITextureDesc& desc,
        const RHIInitialData& initial_data);
    RHIStatus validate_texture_view_desc(
        const RHITextureDesc& texture_desc,
        const RHITextureViewDesc& view_desc);
    RHIStatus validate_buffer_view_desc(
        const RHIBufferDesc& buffer_desc,
        const RHIBufferViewDesc& view_desc);
    RHIStatus validate_shader_desc(const RHIShaderDesc& desc);
    RHIStatus validate_binding_layout_desc(const RHIBindingLayoutDesc& desc);
    RHIStatus validate_graphics_pipeline_desc(const RHIGraphicsPipelineDesc& desc);
}
