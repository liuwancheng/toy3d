#include "rendercore/texture/texture.h"

#include "logging/logger.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool is_depth_stencil_format(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::D16UNorm:
            case PixelFormat::D24UNormS8UInt:
            case PixelFormat::D32Float:
            case PixelFormat::D32FloatS8UInt:
                return true;
            default:
                return false;
            }
        }

        std::uint32_t maximum_mip_count(std::uint32_t width, std::uint32_t height)
        {
            std::uint32_t extent = std::max(width, height);
            std::uint32_t count = 0;
            while (extent != 0)
            {
                ++count;
                extent >>= 1U;
            }
            return count;
        }

        bool calculate_required_slice_pitch(PixelFormat format, std::uint32_t height, std::size_t row_pitch,
                                            std::uint64_t& required_slice_pitch)
        {
            required_slice_pitch = 0;
            const std::uint32_t block_height = pixel_format_block_height(format);
            if (height == 0 || block_height == 0)
            {
                return false;
            }

            std::uint64_t row_count = (static_cast<std::uint64_t>(height) + block_height - 1U) / block_height;
            if ((format == PixelFormat::PVRTC2 || format == PixelFormat::PVRTC4) && row_count < 2U)
            {
                row_count = 2U;
            }
            const std::uint64_t row_pitch_u64 = static_cast<std::uint64_t>(row_pitch);
            if (row_count > std::numeric_limits<std::uint64_t>::max() / row_pitch_u64)
            {
                return false;
            }
            required_slice_pitch = row_count * row_pitch_u64;
            return true;
        }
    } // namespace

    bool TextureDesc::validate(std::string& error) const
    {
        error.clear();
        if (width == 0 || height == 0)
        {
            error = "Texture2D width and height must be non-zero";
            return false;
        }
        if (format == PixelFormat::Unknown || format == PixelFormat::Max || pixel_format_bytes_per_block(format) == 0)
        {
            error = "Texture2D requires a valid GPU-ready PixelFormat";
            return false;
        }
        if (is_depth_stencil_format(format))
        {
            error = "Asset Texture does not represent depth/stencil resources";
            return false;
        }
        if (mip_pixels.empty() || mip_pixels.size() > maximum_mip_count(width, height))
        {
            error = "Texture2D mip count is outside the extent-derived range";
            return false;
        }
        if (row_pitches.size() != mip_pixels.size() || slice_pitches.size() != mip_pixels.size())
        {
            error = "Texture2D mip pixels and pitch arrays must have equal counts";
            return false;
        }

        for (std::size_t mip = 0; mip < mip_pixels.size(); ++mip)
        {
            const std::uint32_t mip_width = std::max(1U, width >> static_cast<std::uint32_t>(mip));
            const std::uint32_t mip_height = std::max(1U, height >> static_cast<std::uint32_t>(mip));
            std::uint64_t minimum_row_pitch = 0;
            if (!pixel_format_calculate_minimum_row_pitch(format, mip_width, minimum_row_pitch) ||
                static_cast<std::uint64_t>(row_pitches[mip]) < minimum_row_pitch ||
                row_pitches[mip] % pixel_format_bytes_per_block(format) != 0)
            {
                error = "Texture2D mip row pitch must cover complete format blocks";
                return false;
            }

            std::uint64_t required_slice_pitch = 0;
            if (!calculate_required_slice_pitch(format, mip_height, row_pitches[mip], required_slice_pitch) ||
                static_cast<std::uint64_t>(slice_pitches[mip]) < required_slice_pitch ||
                slice_pitches[mip] % row_pitches[mip] != 0)
            {
                error = "Texture2D mip slice pitch must cover complete padded rows";
                return false;
            }
            if (mip_pixels[mip].size() < slice_pitches[mip])
            {
                error = "Texture2D mip payload is smaller than its slice pitch";
                return false;
            }
        }
        return true;
    }

    std::shared_ptr<const Texture> Texture::create(TextureDesc desc)
    {
        std::string error;
        if (!desc.validate(error))
        {
            TOY_LOG_ERROR("Invalid TextureDesc: {}.", error);
            return nullptr;
        }

        Texture texture(std::move(desc));
        return std::make_shared<Texture>(std::move(texture));
    }

} // namespace toy3d
