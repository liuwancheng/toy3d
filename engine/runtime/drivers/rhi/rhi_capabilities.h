#pragma once

#include "drivers/rhi/rhi_public_definitions.h"

#include <cstdint>

namespace toy3d
{
    struct RHICapabilities
    {
        bool compute_dispatch = false;
        bool storage_resources = false;
        bool indirect_draw = false;
        bool geometry_shader = false;
        bool tessellation_shader = false;
        bool timestamp_queries = false;
        bool async_compute_queue = false;
        bool parallel_command_recording = false;
    };

    struct RHILimits
    {
        std::uint32_t max_color_attachments = 1;
        std::uint32_t max_vertex_buffers = 1;
        std::uint32_t max_texture_dimension_2d = 1;
        std::uint32_t max_texture_array_layers = 1;
        std::uint32_t max_uniform_buffer_size = 1;
        std::uint32_t max_binding_slots_per_group = 1;
        std::uint32_t max_dynamic_uniform_buffers = 1;
        std::uint32_t max_sampler_anisotropy = 1;
        std::uint64_t uniform_buffer_offset_alignment = 1;
        std::uint64_t storage_buffer_offset_alignment = 1;
        std::uint64_t texture_upload_alignment = 1;
    };

    enum class RHIFormatUsage : std::uint32_t
    {
        None = 0,
        Sampled = 1U << 0,
        Storage = 1U << 1,
        RenderTarget = 1U << 2,
        DepthStencil = 1U << 3,
        VertexBuffer = 1U << 4,
        CopySource = 1U << 5,
        CopyDestination = 1U << 6
    };
    ENUM_CLASS_FLAGS(RHIFormatUsage)

    struct RHIFormatCapabilities
    {
        RHIFormatUsage usage = RHIFormatUsage::None;
        std::uint32_t supported_sample_counts = 1U;
    };
} // namespace toy3d
