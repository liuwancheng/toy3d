#include "pixel_format/pixel_format.h"

#include <limits>

namespace toy3d
{
    std::uint32_t pixel_format_block_width(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::BC1UNorm:
        case PixelFormat::BC2UNorm:
        case PixelFormat::BC3UNorm:
        case PixelFormat::PVRTC4:
        case PixelFormat::ASTC4x4:
            return 4;
        case PixelFormat::PVRTC2:
        case PixelFormat::ASTC8x8:
            return 8;
        case PixelFormat::ASTC6x6:
            return 6;
        case PixelFormat::ASTC12x12:
            return 12;
        case PixelFormat::UYVY:
            return 2;
        case PixelFormat::Unknown:
        case PixelFormat::Max:
            return 0;
        default:
            return 1;
        }
    }

    std::uint32_t pixel_format_block_height(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::BC1UNorm:
        case PixelFormat::BC2UNorm:
        case PixelFormat::BC3UNorm:
        case PixelFormat::PVRTC2:
        case PixelFormat::PVRTC4:
        case PixelFormat::ASTC4x4:
            return 4;
        case PixelFormat::ASTC6x6:
            return 6;
        case PixelFormat::ASTC8x8:
            return 8;
        case PixelFormat::ASTC12x12:
            return 12;
        case PixelFormat::Unknown:
        case PixelFormat::Max:
            return 0;
        default:
            return 1;
        }
    }

    std::uint32_t pixel_format_bytes_per_block(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8UNorm:
        case PixelFormat::R8SNorm:
            return 1;
        case PixelFormat::R16Float:
        case PixelFormat::R16UInt:
            return 2;
        case PixelFormat::R16G16Float:
        case PixelFormat::R8G8B8A8UNorm:
        case PixelFormat::R8G8B8A8UNormSRGB:
        case PixelFormat::B8G8R8A8UNorm:
        case PixelFormat::B8G8R8A8UNormSRGB:
        case PixelFormat::R8G8B8A8SNorm:
        case PixelFormat::R10G10B10A2UNorm:
        case PixelFormat::R11G11B10Float:
        case PixelFormat::R32Float:
        case PixelFormat::R32UInt:
        case PixelFormat::HDR:
        case PixelFormat::D24UNormS8UInt:
        case PixelFormat::D32Float:
        case PixelFormat::UYVY:
            return 4;
        case PixelFormat::R16G16B16A16Float:
        case PixelFormat::R32G32Float:
        case PixelFormat::D32FloatS8UInt:
            return 8;
        case PixelFormat::R32G32B32Float:
            return 12;
        case PixelFormat::R32G32B32A32Float:
            return 16;
        case PixelFormat::BC1UNorm:
        case PixelFormat::PVRTC2:
        case PixelFormat::PVRTC4:
            return 8;
        case PixelFormat::BC2UNorm:
        case PixelFormat::BC3UNorm:
        case PixelFormat::ASTC4x4:
        case PixelFormat::ASTC6x6:
        case PixelFormat::ASTC8x8:
        case PixelFormat::ASTC12x12:
            return 16;
        case PixelFormat::D16UNorm:
            return 2;
        case PixelFormat::Unknown:
        case PixelFormat::Max:
            return 0;
        }
        return 0;
    }

    bool pixel_format_is_block_compressed(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::BC1UNorm:
        case PixelFormat::BC2UNorm:
        case PixelFormat::BC3UNorm:
        case PixelFormat::PVRTC2:
        case PixelFormat::PVRTC4:
        case PixelFormat::ASTC4x4:
        case PixelFormat::ASTC6x6:
        case PixelFormat::ASTC8x8:
        case PixelFormat::ASTC12x12:
            return true;
        default:
            return false;
        }
    }

    bool pixel_format_calculate_minimum_row_pitch(PixelFormat format, std::uint32_t width,
                                                  std::uint64_t& row_pitch) noexcept
    {
        row_pitch = 0;
        const std::uint32_t block_width = pixel_format_block_width(format);
        const std::uint32_t bytes_per_block = pixel_format_bytes_per_block(format);
        if (width == 0 || block_width == 0 || bytes_per_block == 0)
        {
            return false;
        }
        std::uint64_t block_count = (static_cast<std::uint64_t>(width) + block_width - 1U) / block_width;
        if ((format == PixelFormat::PVRTC2 || format == PixelFormat::PVRTC4) && block_count < 2U)
        {
            // PVRTC images reserve at least two blocks along each dimension,
            // even when their logical extent fits in one block.
            block_count = 2U;
        }
        if (block_count > std::numeric_limits<std::uint64_t>::max() / bytes_per_block)
        {
            return false;
        }
        row_pitch = block_count * bytes_per_block;
        return true;
    }

    bool pixel_format_calculate_minimum_slice_pitch(PixelFormat format, std::uint32_t width, std::uint32_t height,
                                                    std::uint64_t& slice_pitch) noexcept
    {
        slice_pitch = 0;
        std::uint64_t row_pitch = 0;
        const std::uint32_t block_height = pixel_format_block_height(format);
        if (height == 0 || block_height == 0 || !pixel_format_calculate_minimum_row_pitch(format, width, row_pitch))
        {
            return false;
        }
        std::uint64_t row_count = (static_cast<std::uint64_t>(height) + block_height - 1U) / block_height;
        if ((format == PixelFormat::PVRTC2 || format == PixelFormat::PVRTC4) && row_count < 2U)
        {
            row_count = 2U;
        }
        if (row_count > std::numeric_limits<std::uint64_t>::max() / row_pitch)
        {
            return false;
        }
        slice_pitch = row_count * row_pitch;
        return true;
    }
} // namespace toy3d
