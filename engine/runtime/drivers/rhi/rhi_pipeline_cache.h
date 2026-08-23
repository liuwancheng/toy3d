#pragma once

#include "drivers/rhi/rhi_resource.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class RHIDevice;

    struct RHIShaderKey
    {
        RHIShaderStage stage = RHIShaderStage::Vertex;
        std::string target;
        std::string entry_point;
        std::array<std::uint64_t, 2> content_hash = {0, 0};
        // Full bytecode equality protects the cache from a caller-supplied
        // content-hash collision without putting the bytecode in the hash path.
        std::vector<std::uint8_t> bytecode;
        std::vector<RHIShaderVertexInputReflection> vertex_inputs;

        bool operator==(const RHIShaderKey& other) const;
    };

    // A pointer-free, stable identity for a logical graphics pipeline. Native
    // pipeline caches may build their own keys from this common identity.
    struct RHIGraphicsPipelineKey
    {
        RHIShaderKey vertex_shader;
        RHIShaderKey pixel_shader;
        RHIBindingLayoutDesc binding_layout;
        RHIPrimitiveTopology primitive_topology = RHIPrimitiveTopology::TriangleList;
        std::vector<RHIGraphicsPipelineDesc::VertexBufferLayout> vertex_buffers;
        std::vector<RHIGraphicsPipelineDesc::VertexAttribute> vertex_attributes;
        RHIGraphicsPipelineDesc::RasterizationState rasterization;
        RHIGraphicsPipelineDesc::DepthStencilState depth_stencil;
        std::array<PixelFormat, RHI_MAX_COLOR_ATTACHMENTS> color_formats = {};
        std::array<
            RHIGraphicsPipelineDesc::ColorBlendAttachmentState,
            RHI_MAX_COLOR_ATTACHMENTS> color_blend_attachments = {};
        std::uint32_t color_attachment_count = 0;
        PixelFormat depth_stencil_format = PixelFormat::Unknown;
        std::uint32_t sample_count = 1;

        bool operator==(const RHIGraphicsPipelineKey& other) const;
    };

    RHIGraphicsPipelineDesc canonicalize_graphics_pipeline_desc(
        const RHIGraphicsPipelineDesc& desc);
    RHIGraphicsPipelineKey make_graphics_pipeline_key(
        const RHIGraphicsPipelineDesc& canonical_desc);
    // Used only to select an in-memory bucket. Persistent backend cache files
    // must use their own versioned serialization of RHIGraphicsPipelineKey.
    std::size_t hash_graphics_pipeline_key(const RHIGraphicsPipelineKey& key);

    class RHIGraphicsPipelineCache final
    {
    public:
        using CreateFunction = std::function<
            RHIResult<RHIGraphicsPipelineRef>(const RHIGraphicsPipelineDesc&)>;

        RHIGraphicsPipelineCache();
        ~RHIGraphicsPipelineCache();

        RHIGraphicsPipelineCache(const RHIGraphicsPipelineCache&) = delete;
        RHIGraphicsPipelineCache& operator=(const RHIGraphicsPipelineCache&) = delete;

    private:
        friend class RHIDevice;

        RHIResult<RHIGraphicsPipelineRef> get_or_create(
            const RHIGraphicsPipelineDesc& desc,
            const CreateFunction& create_function);
        void clear();

        class Impl;
        std::unique_ptr<Impl> implementation;
    };
}
