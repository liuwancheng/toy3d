#pragma once

#include <cstdint>

namespace toy3d
{
    // Stateless bridge: this GPU-ready format identity is shared by Cook,
    // Editor preview, runtime resources and public RHI descriptors. Native API
    // format values remain backend-local.
    enum class PixelFormat : std::uint16_t
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
        Max
    };

    std::uint32_t pixel_format_block_width(PixelFormat format) noexcept;
    std::uint32_t pixel_format_block_height(PixelFormat format) noexcept;
    std::uint32_t pixel_format_bytes_per_block(PixelFormat format) noexcept;
    bool pixel_format_is_block_compressed(PixelFormat format) noexcept;
    bool pixel_format_calculate_minimum_row_pitch(
        PixelFormat format,
        std::uint32_t width,
        std::uint64_t& row_pitch) noexcept;
    bool pixel_format_calculate_minimum_slice_pitch(
        PixelFormat format,
        std::uint32_t width,
        std::uint32_t height,
        std::uint64_t& slice_pitch) noexcept;
}
